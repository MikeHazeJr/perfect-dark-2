/*
 * test_projectile_rules.cpp -- the modules of a projectile program and the
 * original game's rules (port/src/projectile_rules.c;
 * context/designs/modding/projectile-events.md, slices 3 to 6).
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
#include "projectile_events.h"
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
	return kind && (projectileRulesModuleBit(kind) != 0 || projectileRulesGateBit(kind) != 0) ? 1 : 0;
}

struct Choice {
	u32 modules = 0;
	u32 passing = 0; // the gates that pass (PROJECTILE_RULE_GATE_* bits)
	s32 explosion = PROJECTILE_EXPLOSION_OWN;
};

s32 runModule(const char *kind, const char *, const mission_graph_params_t *params,
	const mission_graph_event_t *, void *user)
{
	Choice *choice = static_cast<Choice *>(user);
	const u32 gate = projectileRulesGateBit(kind);
	if (gate) {
		return (choice->passing & gate) ? 1 : 0;
	}
	const u32 bit = projectileRulesModuleBit(kind);
	choice->modules |= bit;
	if (bit == PROJECTILE_MODULE_EXPLODE) {
		choice->explosion = projectileRulesExplosionFromText(
			missionGraphParamText(params, PROJECTILE_EXPLODE_TYPE_PARAM));
	}
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
	CHECK(PROJECTILE_MODULE_COUNT == 20);
	CHECK(projectileRulesModuleKind(0) == nullptr);
	CHECK(projectileRulesModuleKind(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_PIERCE) == nullptr);
	CHECK(projectileRulesModuleKind(1u << PROJECTILE_MODULE_COUNT) == nullptr);
	CHECK(projectileRulesModuleBit("og.projectile.stick") == PROJECTILE_MODULE_STICK);
	CHECK(projectileRulesModuleBit("og.projectile.explode") == PROJECTILE_MODULE_EXPLODE);
	CHECK(projectileRulesModuleBit("og.projectile.nbomb_storm") == PROJECTILE_MODULE_NBOMB_STORM);
	CHECK(projectileRulesModuleBit("og.projectile.deploy_sentry") == PROJECTILE_MODULE_DEPLOY_SENTRY);
	CHECK(projectileRulesModuleBit("og.projectile.sentry_fire") == PROJECTILE_MODULE_SENTRY_FIRE);
	// Slice 6: the arms, in the order weaponTick tests them.
	CHECK(projectileRulesModuleBit("og.projectile.arm_fuse") == PROJECTILE_MODULE_ARM_FUSE);
	CHECK(projectileRulesModuleBit("og.projectile.arm_wall_fuse") == PROJECTILE_MODULE_ARM_WALL_FUSE);
	CHECK(projectileRulesModuleBit("og.projectile.arm_struck_fuse") == PROJECTILE_MODULE_ARM_STRUCK_FUSE);
	CHECK(projectileRulesModuleBit("og.projectile.arm_mine_fuse") == PROJECTILE_MODULE_ARM_MINE_FUSE);
	CHECK(projectileRulesModuleBit("og.projectile.arm_remote") == PROJECTILE_MODULE_ARM_REMOTE);
	CHECK(projectileRulesModuleBit("og.projectile.arm_proximity") == PROJECTILE_MODULE_ARM_PROXIMITY);
	CHECK(projectileRulesModuleBit("og.projectile.arm_bolt") == PROJECTILE_MODULE_ARM_BOLT);
	CHECK(PROJECTILE_MODULE_ARMS == (PROJECTILE_MODULE_ALL & ~((1u << 13) - 1)));
	CHECK(PROJECTILE_MODULE_ARM_FUSE < PROJECTILE_MODULE_ARM_WALL_FUSE);
	CHECK(PROJECTILE_MODULE_ARM_PROXIMITY < PROJECTILE_MODULE_ARM_BOLT);
	// What the runtime keeps beside an object's arms is no module and no gate.
	CHECK((PROJECTILE_ARMS_PROGRAM & (PROJECTILE_MODULE_ALL | PROJECTILE_RULE_GATE_ALL)) == 0);
	// The module bits stay below the gate bits.
	CHECK((PROJECTILE_MODULE_ALL & PROJECTILE_RULE_GATE_ALL) == 0);
	// Slice 2's name for the fuse's outcome is gone: explode and nbomb_storm say what goes off.
	CHECK(projectileRulesModuleBit("og.projectile.fuse_expired") == 0);
	CHECK(projectileRulesModuleBit("og.projectile.vanish") == 0);
	CHECK(projectileRulesModuleBit(PROJECTILE_GATE_FUSE_ARMED_KIND) == 0);
	CHECK(projectileRulesModuleBit(nullptr) == 0);
}

TEST_CASE("projectile rules: every gate has one kind, one bit and its events", "[projectile][rules]")
{
	u32 seen = 0;
	for (s32 g = 0; g < PROJECTILE_GATE_COUNT; g++) {
		const u32 gate = PROJECTILE_RULE_GATE_FUSE_ARMED << g;
		const char *kind = projectileRulesGateKind(gate);
		REQUIRE(kind != nullptr);
		CHECK(std::string(kind).rfind("gate.projectile.", 0) == 0);
		CHECK(projectileRulesGateBit(kind) == gate);
		// A gate bit is never a module bit: the original's rule carries both.
		CHECK((gate & PROJECTILE_MODULE_ALL) == 0);
		seen |= gate;
	}
	CHECK(seen == PROJECTILE_RULE_GATE_ALL);
	CHECK(PROJECTILE_GATE_COUNT == 4);
	CHECK(projectileRulesGateBit(PROJECTILE_GATE_FUSE_ARMED_KIND) == PROJECTILE_RULE_GATE_FUSE_ARMED);
	CHECK(projectileRulesGateBit(PROJECTILE_GATE_TRIGGER_CLOSE_KIND) == PROJECTILE_RULE_GATE_TRIGGER_CLOSE);
	CHECK(projectileRulesGateBit(PROJECTILE_GATE_TRIGGER_CHARACTER_KIND) ==
		PROJECTILE_RULE_GATE_TRIGGER_CHARACTER);
	CHECK(projectileRulesGateBit(PROJECTILE_GATE_DAMAGE_NOT_BLAST_KIND) ==
		PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST);
	CHECK(projectileRulesGateBit("gate.mission.phase") == 0);
	CHECK(projectileRulesGateBit("og.projectile.stick") == 0);
	CHECK(projectileRulesGateBit(nullptr) == 0);
	CHECK(projectileRulesGateKind(0) == nullptr);
	CHECK(projectileRulesGateKind(PROJECTILE_RULE_GATE_ALL) == nullptr);
	CHECK(projectileRulesGateKind(PROJECTILE_MODULE_STICK) == nullptr);

	// A gate answers where its event carries what it reads.
	for (s32 i = 0; projectileRulesEventAt(i) >= 0; i++) {
		const s32 kind = projectileRulesEventAt(i);
		INFO(projectileRulesEventName(kind));
		CHECK(projectileRulesGateApplies(PROJECTILE_RULE_GATE_FUSE_ARMED, kind) == 1);
		CHECK(projectileRulesGateApplies(PROJECTILE_RULE_GATE_TRIGGER_CLOSE, kind) ==
			(kind == MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY ? 1 : 0));
		CHECK(projectileRulesGateApplies(PROJECTILE_RULE_GATE_TRIGGER_CHARACTER, kind) ==
			(kind == MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY ? 1 : 0));
		CHECK(projectileRulesGateApplies(PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST, kind) ==
			(kind == MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED ? 1 : 0));
		// The original never asks a gate its event cannot answer.
		for (s32 weaponnum = 0; weaponnum < 0x60; weaponnum++) {
			for (s32 func = FUNC_PRIMARY; func <= FUNC_2; func++) {
				const u32 native = projectileRulesNative(kind, weaponnum, func, 1);
				for (s32 g = 0; g < PROJECTILE_GATE_COUNT; g++) {
					const u32 gate = PROJECTILE_RULE_GATE_FUSE_ARMED << g;
					if (native & gate) {
						CHECK(projectileRulesGateApplies(gate, kind) == 1);
						// A gate stands in front of something.
						CHECK((native & PROJECTILE_MODULE_ALL) != 0);
					}
				}
			}
		}
	}
	CHECK(projectileRulesGateApplies(PROJECTILE_RULE_GATE_FUSE_ARMED, MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == 0);
	CHECK(projectileRulesGateApplies(0, MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY) == 0);
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
	CHECK(count == 16);
	CHECK(projectileRulesEventAt(-1) == -1);
	CHECK(projectileRulesEventName(MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == nullptr);
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PICKUP_TOUCHED) == 0);
	CHECK(missionGraphEventIsProjectile(MISSION_GRAPH_EVENT_PROJECTILE_ARMED + 1) == 0);

	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED) ==
		(PROJECTILE_MODULE_EXPLODE | PROJECTILE_MODULE_NBOMB_STORM));
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT) ==
		(PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_WALL_HUG | PROJECTILE_MODULE_PIERCE |
			PROJECTILE_MODULE_IMPACT));
	// Everything that can set a fuse off spends it; the fuse rule then says what goes off.
	for (s32 kind : { MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED,
			MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY,
			MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED }) {
		CHECK(projectileRulesEventModules(kind) == PROJECTILE_MODULE_SPEND_FUSE);
	}
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_LANDED) ==
		(PROJECTILE_MODULE_SHIELD_DEVICE | PROJECTILE_MODULE_LAND_BOLT | PROJECTILE_MODULE_LAND_KNIFE |
			PROJECTILE_MODULE_POISON_TARGET));
	// Slice 5: what a throw makes, and what an aimed sentry does.
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_THROWN) == PROJECTILE_MODULE_DEPLOY_SENTRY);
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED) ==
		PROJECTILE_MODULE_SENTRY_FIRE);
	// Slice 6: the arm an object runs, and nothing else.
	CHECK(projectileRulesEventModules(MISSION_GRAPH_EVENT_PROJECTILE_ARMED) == PROJECTILE_MODULE_ARMS);
	CHECK(std::string(projectileRulesEventName(MISSION_GRAPH_EVENT_PROJECTILE_ARMED)) == "event.projectile.armed");
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
	// The records choose nothing.
	CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED, WEAPON_ROCKET, FUNC_PRIMARY) == 0);
	CHECK(at(MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET, WEAPON_BOLT, FUNC_PRIMARY) == 0);
}

TEST_CASE("projectile rules: the original game's fuses and what sets them off", "[projectile][rules]")
{
	const auto at = [](s32 kind, s32 weaponnum, s32 func) {
		return projectileRulesNative(kind, weaponnum, func, 0);
	};
	const s32 fuse = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	const s32 proximity = MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY;
	const s32 signal = MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL;
	const s32 damaged = MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED;
	const u32 explode = PROJECTILE_MODULE_EXPLODE;
	const u32 storm = PROJECTILE_MODULE_NBOMB_STORM;
	const u32 spend = PROJECTILE_MODULE_SPEND_FUSE;

	// What goes off, for the objects weaponTick gives a fuse.
	CHECK(at(fuse, WEAPON_GRENADE, FUNC_PRIMARY) == explode);   // its timer
	CHECK(at(fuse, WEAPON_GRENADE, FUNC_SECONDARY) == explode); // the proximity pinball
	for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY, FUNC_2 }) {
		CHECK(at(fuse, WEAPON_GRENADEROUND, func) == explode);
		CHECK(at(fuse, WEAPON_ROCKET, func) == explode);
		CHECK(at(fuse, WEAPON_HOMINGROCKET, func) == explode);
		CHECK(at(fuse, WEAPON_SKROCKET, func) == explode);
		CHECK(at(fuse, WEAPON_REMOTEMINE, func) == explode);
		CHECK(at(fuse, WEAPON_PROXIMITYMINE, func) == explode);
	}
	CHECK(at(fuse, WEAPON_TIMEDMINE, FUNC_PRIMARY) == explode);
	CHECK(at(fuse, WEAPON_TIMEDMINE, FUNC_SECONDARY) == 0); // its timer counts for the first function only
	CHECK(at(fuse, WEAPON_DRAGON, FUNC_SECONDARY) == explode);
	CHECK(at(fuse, WEAPON_DRAGON, FUNC_PRIMARY) == 0);
	// The N-bomb becomes a storm, thrown or as a proximity item.
	CHECK(at(fuse, WEAPON_NBOMB, FUNC_PRIMARY) == storm);
	CHECK(at(fuse, WEAPON_NBOMB, FUNC_SECONDARY) == storm);
	// Nothing else has a fuse: a knife, a bolt, a planted device, a gun.
	for (s32 weaponnum : { WEAPON_COMBATKNIFE, WEAPON_BOLT, WEAPON_ECMMINE, WEAPON_COMMSRIDER,
			WEAPON_TRACERBUG, WEAPON_TARGETAMPLIFIER, WEAPON_LAPTOPGUN, WEAPON_FALCON2,
			WEAPON_ROCKETLAUNCHER, WEAPON_DEVASTATOR, WEAPON_CROSSBOW }) {
		for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY }) {
			CHECK(at(fuse, weaponnum, func) == 0);
		}
	}

	// The explosion: the object's own, but for the Super Dragon's grenade
	// and the Dragon's self-destruct.
	CHECK(projectileRulesNativeExplosion(WEAPON_GRENADEROUND, FUNC_2) == EXPLOSIONTYPE_SDGRENADE);
	CHECK(projectileRulesNativeExplosion(WEAPON_GRENADEROUND, FUNC_PRIMARY) == PROJECTILE_EXPLOSION_OWN);
	CHECK(projectileRulesNativeExplosion(WEAPON_GRENADEROUND, FUNC_SECONDARY) == PROJECTILE_EXPLOSION_OWN);
	CHECK(projectileRulesNativeExplosion(WEAPON_DRAGON, FUNC_SECONDARY) == EXPLOSIONTYPE_DRAGONBOMBSPY);
	for (s32 weaponnum : { WEAPON_GRENADE, WEAPON_ROCKET, WEAPON_HOMINGROCKET, WEAPON_SKROCKET,
			WEAPON_TIMEDMINE, WEAPON_REMOTEMINE, WEAPON_PROXIMITYMINE }) {
		CHECK(projectileRulesNativeExplosion(weaponnum, FUNC_PRIMARY) == PROJECTILE_EXPLOSION_OWN);
		CHECK(projectileRulesNativeExplosion(weaponnum, FUNC_2) == PROJECTILE_EXPLOSION_OWN);
	}

	// Proximity: the four armed items. The Dragon goes off for anything in
	// the watch list's reach; the others only within 250 units; the grenade
	// only for a character.
	CHECK(at(proximity, WEAPON_DRAGON, FUNC_SECONDARY) == spend);
	CHECK(at(proximity, WEAPON_PROXIMITYMINE, FUNC_PRIMARY) ==
		(spend | PROJECTILE_RULE_GATE_TRIGGER_CLOSE));
	CHECK(at(proximity, WEAPON_NBOMB, FUNC_SECONDARY) == (spend | PROJECTILE_RULE_GATE_TRIGGER_CLOSE));
	CHECK(at(proximity, WEAPON_GRENADE, FUNC_SECONDARY) ==
		(spend | PROJECTILE_RULE_GATE_TRIGGER_CLOSE | PROJECTILE_RULE_GATE_TRIGGER_CHARACTER));
	for (s32 weaponnum : { WEAPON_GRENADE, WEAPON_NBOMB, WEAPON_DRAGON, WEAPON_TIMEDMINE,
			WEAPON_REMOTEMINE, WEAPON_ROCKET }) {
		CHECK(at(proximity, weaponnum, FUNC_PRIMARY) == 0);
	}
	// Remote: only a remote mine listens for the detonator.
	CHECK(at(signal, WEAPON_REMOTEMINE, FUNC_PRIMARY) == spend);
	for (s32 weaponnum : { WEAPON_TIMEDMINE, WEAPON_PROXIMITYMINE, WEAPON_GRENADE, WEAPON_ROCKET }) {
		CHECK(at(signal, weaponnum, FUNC_PRIMARY) == 0);
	}
	// Damage: objDamage's explosives; a homing rocket ignores a blast.
	for (s32 weaponnum : { WEAPON_GRENADE, WEAPON_TIMEDMINE, WEAPON_REMOTEMINE, WEAPON_PROXIMITYMINE,
			WEAPON_ROCKET, WEAPON_GRENADEROUND }) {
		for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY, FUNC_2 }) {
			CHECK(at(damaged, weaponnum, func) == spend);
		}
	}
	CHECK(at(damaged, WEAPON_HOMINGROCKET, FUNC_PRIMARY) ==
		(spend | PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST));
	CHECK(at(damaged, WEAPON_DRAGON, FUNC_SECONDARY) == spend);
	CHECK(at(damaged, WEAPON_DRAGON, FUNC_PRIMARY) == 0);
	// Not explosives: the N-bomb, the Slayer's own rocket object, a bolt, a
	// knife, a planted device, a gun.
	for (s32 weaponnum : { WEAPON_NBOMB, WEAPON_SKROCKET, WEAPON_BOLT, WEAPON_COMBATKNIFE,
			WEAPON_ECMMINE, WEAPON_FALCON2 }) {
		CHECK(at(damaged, weaponnum, FUNC_PRIMARY) == 0);
	}
	// Every explosive has a fuse to spend.
	for (s32 weaponnum = 0; weaponnum < 0x60; weaponnum++) {
		for (s32 func = FUNC_PRIMARY; func <= FUNC_SECONDARY; func++) {
			if (weaponnum == WEAPON_TIMEDMINE && func == FUNC_SECONDARY) {
				continue; // never thrown: the mine's second function is its threat detector
			}
			for (s32 kind : { proximity, signal, damaged }) {
				if (at(kind, weaponnum, func) & spend) {
					INFO("weapon " << weaponnum << " func " << func << " event " << kind);
					CHECK(at(fuse, weaponnum, func) != 0);
				}
			}
		}
	}
}

TEST_CASE("projectile rules: the original game's sentry rules", "[projectile][rules]")
{
	const s32 thrown = MISSION_GRAPH_EVENT_PROJECTILE_THROWN;
	const s32 aimed = MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED;

	// At a throw the weapon is the thrower's: a thrown Laptop gun becomes
	// its sentry, whichever function index asks; nothing else does.
	for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY }) {
		CHECK(projectileRulesNative(thrown, WEAPON_LAPTOPGUN, func, 0) == PROJECTILE_MODULE_DEPLOY_SENTRY);
	}
	for (s32 weaponnum = 0; weaponnum < 0x60; weaponnum++) {
		if (weaponnum != WEAPON_LAPTOPGUN) {
			INFO("weapon " << weaponnum);
			CHECK(projectileRulesNative(thrown, weaponnum, FUNC_SECONDARY, 0) == 0);
		}
	}
	// An aimed sentry fires, and so does every other autogun: one answer.
	for (s32 weaponnum : { -1, 0, (s32)WEAPON_LAPTOPGUN, (s32)WEAPON_GRENADE }) {
		CHECK(projectileRulesNative(aimed, weaponnum, FUNC_PRIMARY, 0) == PROJECTILE_MODULE_SENTRY_FIRE);
	}
	// The thrown Laptop gun is no weapon object, so it has no object rules.
	s32 objweapon = 0;
	s32 objfunc = 0;
	projectileRulesLaunchedObject(WEAPON_LAPTOPGUN, FUNC_SECONDARY, 1, 0, &objweapon, &objfunc);
	CHECK(objweapon == -1);

	// The Laptop gun's wiring, as the extractor writes it, through the runner.
	const std::string graph = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "motion", "kind": "projectile.motion", "params": {} },
    { "id": "thrown", "kind": "event.projectile.thrown" },
    { "id": "thrown_deploy_sentry", "kind": "og.projectile.deploy_sentry" },
    { "id": "sentry_aimed", "kind": "event.projectile.sentry_aimed" },
    { "id": "sentry_aimed_sentry_fire", "kind": "og.projectile.sentry_fire" }
  ],
  "edges": [
    { "from": "thrown", "output": "out", "to": "thrown_deploy_sentry", "input": "exec" },
    { "from": "sentry_aimed", "output": "out", "to": "sentry_aimed_sentry_fire", "input": "exec" }
  ]
})";
	char err[256] = "";
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(graph.data(), graph.size(),
		moduleKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	int thrower = 0;
	const auto fire = [&](s32 kind) {
		mission_graph_event_t event = {};
		event.kind = kind;
		event.instance = &thrower; // a throw has no object yet: its gset stands in
		Choice choice;
		CHECK(missionGraphProgramFire(program, &event, runModule, &choice) >= 0);
		return choice.modules;
	};
	CHECK(fire(thrown) == PROJECTILE_MODULE_DEPLOY_SENTRY);
	CHECK(fire(aimed) == PROJECTILE_MODULE_SENTRY_FIRE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT) == 0);
	// An event still needs an instance.
	mission_graph_event_t bare = {};
	bare.kind = thrown;
	Choice none;
	CHECK(missionGraphProgramFire(program, &bare, runModule, &none) < 0);
	missionGraphProgramFree(program);
}

TEST_CASE("projectile rules: the original game's arms", "[projectile][rules]")
{
	const s32 armed = MISSION_GRAPH_EVENT_PROJECTILE_ARMED;
	const auto arm = [&](s32 weaponnum, s32 func) {
		return projectileRulesNative(armed, weaponnum, func, 0);
	};

	// The countdown: a hand grenade's and the N-bomb's first function, and a
	// grenade round whatever fired it. The Devastator's wall hugger has the
	// wall timer as well.
	CHECK(arm(WEAPON_GRENADE, FUNC_PRIMARY) == PROJECTILE_MODULE_ARM_FUSE);
	CHECK(arm(WEAPON_NBOMB, FUNC_PRIMARY) == PROJECTILE_MODULE_ARM_FUSE);
	CHECK(arm(WEAPON_GRENADEROUND, FUNC_PRIMARY) == PROJECTILE_MODULE_ARM_FUSE);
	CHECK(arm(WEAPON_GRENADEROUND, FUNC_2) == PROJECTILE_MODULE_ARM_FUSE); // the Super Dragon's grenade
	CHECK(arm(WEAPON_GRENADEROUND, FUNC_SECONDARY) == PROJECTILE_MODULE_ARM_WALL_FUSE);
	// A rocket has no countdown: it goes off when something spends its fuse.
	for (s32 weaponnum : { WEAPON_ROCKET, WEAPON_HOMINGROCKET, WEAPON_SKROCKET }) {
		for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY, FUNC_2 }) {
			CHECK(arm(weaponnum, func) == PROJECTILE_MODULE_ARM_STRUCK_FUSE);
		}
	}
	// The mines, and the bolt.
	CHECK(arm(WEAPON_TIMEDMINE, FUNC_PRIMARY) == PROJECTILE_MODULE_ARM_MINE_FUSE);
	CHECK(arm(WEAPON_TIMEDMINE, FUNC_SECONDARY) == 0); // its timer counts for the first function only
	for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY, FUNC_2 }) {
		CHECK(arm(WEAPON_REMOTEMINE, func) == PROJECTILE_MODULE_ARM_REMOTE);
		CHECK(arm(WEAPON_PROXIMITYMINE, func) == PROJECTILE_MODULE_ARM_PROXIMITY);
		CHECK(arm(WEAPON_BOLT, func) == PROJECTILE_MODULE_ARM_BOLT);
	}
	// The other proximity items: the second functions of the Dragon, the
	// grenade and the N-bomb.
	CHECK(arm(WEAPON_DRAGON, FUNC_SECONDARY) == PROJECTILE_MODULE_ARM_PROXIMITY);
	CHECK(arm(WEAPON_GRENADE, FUNC_SECONDARY) == PROJECTILE_MODULE_ARM_PROXIMITY);
	CHECK(arm(WEAPON_NBOMB, FUNC_SECONDARY) == PROJECTILE_MODULE_ARM_PROXIMITY);
	CHECK(arm(WEAPON_DRAGON, FUNC_PRIMARY) == 0);
	// Nothing else has an arm: a knife, a planted device, a gun, a launcher.
	for (s32 weaponnum : { WEAPON_COMBATKNIFE, WEAPON_ECMMINE, WEAPON_COMMSRIDER, WEAPON_TRACERBUG,
			WEAPON_TARGETAMPLIFIER, WEAPON_LAPTOPGUN, WEAPON_FALCON2, WEAPON_ROCKETLAUNCHER,
			WEAPON_ROCKETLAUNCHER_34, WEAPON_DEVASTATOR, WEAPON_CROSSBOW, WEAPON_SUPERDRAGON }) {
		for (s32 func : { FUNC_PRIMARY, FUNC_SECONDARY }) {
			CHECK(arm(weaponnum, func) == 0);
		}
	}

	// The whole table against the chain weaponTick tested before slice 6,
	// arm by arm in its order (propobj.c at 4c1ad8a0).
	const auto chain = [](s32 weaponnum, s32 gunfunc) -> u32 {
		if ((weaponnum == WEAPON_GRENADE && gunfunc == FUNC_PRIMARY) || weaponnum == WEAPON_GRENADEROUND) {
			return weaponnum == WEAPON_GRENADEROUND && gunfunc == FUNC_SECONDARY
				? PROJECTILE_MODULE_ARM_WALL_FUSE : PROJECTILE_MODULE_ARM_FUSE;
		} else if (weaponnum == WEAPON_NBOMB && gunfunc == FUNC_PRIMARY) {
			return PROJECTILE_MODULE_ARM_FUSE;
		} else if (weaponnum == WEAPON_ROCKET
				|| weaponnum == WEAPON_HOMINGROCKET
				|| weaponnum == WEAPON_SKROCKET) {
			return PROJECTILE_MODULE_ARM_STRUCK_FUSE;
		} else if (weaponnum == WEAPON_TIMEDMINE) {
			return gunfunc == FUNC_PRIMARY ? PROJECTILE_MODULE_ARM_MINE_FUSE : 0;
		} else if (weaponnum == WEAPON_REMOTEMINE) {
			return PROJECTILE_MODULE_ARM_REMOTE;
		} else if (weaponnum == WEAPON_PROXIMITYMINE
				|| (weaponnum == WEAPON_DRAGON && gunfunc == FUNC_SECONDARY)
				|| (weaponnum == WEAPON_GRENADE && gunfunc == FUNC_SECONDARY)
				|| (weaponnum == WEAPON_NBOMB && gunfunc == FUNC_SECONDARY)) {
			return PROJECTILE_MODULE_ARM_PROXIMITY;
		} else if (weaponnum == WEAPON_BOLT) {
			return PROJECTILE_MODULE_ARM_BOLT;
		}
		return 0;
	};
	s32 armedObjects = 0;
	for (s32 weaponnum = -1; weaponnum < 0x60; weaponnum++) {
		for (s32 func = FUNC_PRIMARY; func <= FUNC_POISON; func++) {
			const u32 native = arm(weaponnum, func);
			INFO("weapon " << weaponnum << " func " << func);
			CHECK(native == chain(weaponnum, func));
			// One arm or none, and no gate: the original's arm is not conditional.
			CHECK((native & (native - 1)) == 0);
			CHECK((native & ~PROJECTILE_MODULE_ARMS) == 0);
			// sticks_to_wall is the contact rule's; it does not change the arm.
			CHECK(projectileRulesNative(armed, weaponnum, func, 1) == native);
			armedObjects += native != 0 ? 1 : 0;
			// Only an arm asks the fuse rule: whatever the original gives a
			// fuse has an arm that runs one (the bolt's arm runs none).
			if (projectileRulesNative(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, weaponnum, func, 0)) {
				CHECK((native & PROJECTILE_MODULE_ARMS & ~PROJECTILE_MODULE_ARM_BOLT) != 0);
			}
			// The wall hugger's arm and its contact go together.
			CHECK(((projectileRulesNative(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, weaponnum, func, 1)
					& PROJECTILE_MODULE_WALL_HUG) != 0)
				== (native == PROJECTILE_MODULE_ARM_WALL_FUSE));
		}
	}
	// Over the four function numbers: the grenade 2, the N-bomb 2, the grenade
	// round 4, three rockets 12, the timed mine 1, the remote and proximity
	// mines 8, the Dragon 1, the bolt 4.
	CHECK(armedObjects == 34);
	// A mod weapon's number has no arm of the original's: its program arms
	// it, or its v1 records do.
	CHECK(arm(WEAPON_CUSTOM_START, FUNC_PRIMARY) == 0);
	CHECK(arm(0x7f, FUNC_SECONDARY) == 0);
}

TEST_CASE("projectile rules: an object arms itself through the graph runner", "[projectile][rules][graph]")
{
	// A grenade's wiring as the extractor writes it (its arm, then its fuse),
	// with a second arm behind the fuse_armed gate, as a mod might wire one.
	const std::string graph = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "motion", "kind": "projectile.motion", "params": {} },
    { "id": "armed", "kind": "event.projectile.armed" },
    { "id": "armed_arm_fuse", "kind": "og.projectile.arm_fuse" },
    { "id": "armed_fuse_armed", "kind": "gate.projectile.fuse_armed" },
    { "id": "armed_arm_proximity", "kind": "og.projectile.arm_proximity" },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "fuse_explode", "kind": "og.projectile.explode" }
  ],
  "edges": [
    { "from": "armed", "output": "out", "to": "armed_arm_fuse", "input": "exec" },
    { "from": "armed", "output": "out", "to": "armed_fuse_armed", "input": "exec" },
    { "from": "armed_fuse_armed", "output": "fail", "to": "armed_arm_proximity", "input": "exec" },
    { "from": "fuse", "output": "out", "to": "fuse_explode", "input": "exec" }
  ]
})";
	char err[256] = "";
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(graph.data(), graph.size(),
		moduleKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	const s32 armed = MISSION_GRAPH_EVENT_PROJECTILE_ARMED;
	CHECK(missionGraphEventIsProjectile(armed) == 1);
	CHECK(missionGraphProgramHandlesEvent(program, armed) == 1);
	// What registration asks: which arms the event can reach, gates or not.
	CHECK(missionGraphProgramEventReachesModule(program, armed, "og.projectile.arm_fuse") == 1);
	CHECK(missionGraphProgramEventReachesModule(program, armed, "og.projectile.arm_proximity") == 1);
	CHECK(missionGraphProgramEventReachesModule(program, armed, "og.projectile.arm_bolt") == 0);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED,
		"og.projectile.arm_fuse") == 0);

	int instance = 0;
	const auto fire = [&](s32 kind, bool live) {
		mission_graph_event_t event = {};
		event.kind = kind;
		event.instance = &instance;
		Choice choice;
		choice.passing = live ? PROJECTILE_RULE_GATE_FUSE_ARMED : 0;
		CHECK(missionGraphProgramFire(program, &event, runModule, &choice) >= 0);
		return choice.modules;
	};
	// Thrown with a live fuse it counts down; one whose timer is idle when it
	// is first ticked also waits as a proximity item.
	CHECK(fire(armed, true) == PROJECTILE_MODULE_ARM_FUSE);
	CHECK(fire(armed, false) == (PROJECTILE_MODULE_ARM_FUSE | PROJECTILE_MODULE_ARM_PROXIMITY));
	CHECK((fire(armed, false) & ~projectileRulesEventModules(armed)) == 0);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, true) == PROJECTILE_MODULE_EXPLODE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, true) == 0);
	missionGraphProgramFree(program);

	// The arm event is a projectile event: no mission or level graph has it.
	const std::string mission = R"({ "nodes": [ { "id": "a", "kind": "event.projectile.armed" } ], "edges": [] })";
	CHECK(missionGraphProgramCompile(mission.data(), mission.size(), moduleKnown, err, sizeof(err)) == nullptr);
	CHECK(std::string(err).find("belongs to a projectile or entity graph") != std::string::npos);
	CHECK(missionGraphProgramCompileLevel(mission.data(), mission.size(), moduleKnown, err, sizeof(err)) == nullptr);
}

TEST_CASE("projectile rules: the explosion an explode node names", "[projectile][rules]")
{
	CHECK(projectileRulesExplosionFromText(nullptr) == PROJECTILE_EXPLOSION_OWN);
	CHECK(projectileRulesExplosionFromText("21") == EXPLOSIONTYPE_SDGRENADE);
	CHECK(projectileRulesExplosionFromText("23") == EXPLOSIONTYPE_DRAGONBOMBSPY);
	CHECK(projectileRulesExplosionFromText("1") == 1);
	CHECK(projectileRulesExplosionFromText("25") == PROJECTILE_EXPLOSION_TYPE_COUNT - 1);
	// "No explosion", a number past the table, and anything that is no whole number.
	for (const char *bad : { "0", "26", "100", "1000", "", "-1", "2.5", "true", "rocket", " 21", "21 " }) {
		INFO(bad);
		CHECK(projectileRulesExplosionFromText(bad) == PROJECTILE_EXPLOSION_REFUSED);
	}
	CHECK(PROJECTILE_EXPLOSION_OWN < 0);
	CHECK(PROJECTILE_EXPLOSION_REFUSED < 0);
	CHECK(PROJECTILE_EXPLOSION_OWN != PROJECTILE_EXPLOSION_REFUSED);
	// The table's size is the game's.
	CHECK(PROJECTILE_EXPLOSION_TYPE_COUNT == 26);
	REQUIRE(readSource("src/include/game/explosions.h").find("#define EXPLOSIONTYPE_BASE_COUNT 26\n")
		!= std::string::npos);
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
	// A native gate is reached like a module (slice 4: registration asks
	// whether an event reaches a gate it cannot answer).
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST,
		PROJECTILE_GATE_FUSE_ARMED_KIND) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_CONTACT,
		PROJECTILE_GATE_FUSE_ARMED_KIND) == 0);

	int instance = 0;
	const auto fire = [&](s32 kind, bool armed) {
		mission_graph_event_t event = {};
		event.kind = kind;
		event.instance = &instance;
		Choice choice;
		choice.passing = armed ? PROJECTILE_RULE_GATE_FUSE_ARMED : 0;
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

TEST_CASE("projectile rules: a program decides what sets a fuse off and what goes off",
	"[projectile][rules][graph]")
{
	// The proximity grenade's wiring, the homing rocket's damage rule, a
	// remote mine's signal, and the Dragon's and the N-bomb's fuses, as the
	// extractor writes them (two fuse events stand for two archives).
	const std::string graph = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "motion", "kind": "projectile.motion", "params": {} },
    { "id": "proximity", "kind": "event.projectile.proximity" },
    { "id": "proximity_trigger_close", "kind": "gate.projectile.trigger_close" },
    { "id": "proximity_trigger_character", "kind": "gate.projectile.trigger_character" },
    { "id": "proximity_spend_fuse", "kind": "og.projectile.spend_fuse" },
    { "id": "damaged", "kind": "event.projectile.damaged" },
    { "id": "damaged_damage_not_blast", "kind": "gate.projectile.damage_not_blast" },
    { "id": "damaged_spend_fuse", "kind": "og.projectile.spend_fuse" },
    { "id": "remote_signal", "kind": "event.projectile.remote_signal" },
    { "id": "remote_signal_spend_fuse", "kind": "og.projectile.spend_fuse" },
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "fuse_explode", "kind": "og.projectile.explode", "explosion_type": 23 }
  ],
  "edges": [
    { "from": "proximity", "output": "out", "to": "proximity_trigger_close", "input": "exec" },
    { "from": "proximity_trigger_close", "output": "pass", "to": "proximity_trigger_character", "input": "exec" },
    { "from": "proximity_trigger_character", "output": "pass", "to": "proximity_spend_fuse", "input": "exec" },
    { "from": "damaged", "output": "out", "to": "damaged_damage_not_blast", "input": "exec" },
    { "from": "damaged_damage_not_blast", "output": "pass", "to": "damaged_spend_fuse", "input": "exec" },
    { "from": "remote_signal", "output": "out", "to": "remote_signal_spend_fuse", "input": "exec" },
    { "from": "fuse", "output": "out", "to": "fuse_explode", "input": "exec" }
  ]
})";
	char err[256] = "";
	mission_graph_program_t *program = missionGraphProgramCompileProjectile(graph.data(), graph.size(),
		moduleKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	for (s32 kind : { MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED,
			MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED }) {
		CHECK(missionGraphProgramHandlesEvent(program, kind) == 1);
	}
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY,
		PROJECTILE_GATE_TRIGGER_CHARACTER_KIND) == 1);
	CHECK(missionGraphProgramEventReachesModule(program, MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED,
		PROJECTILE_GATE_TRIGGER_CHARACTER_KIND) == 0);

	int instance = 0;
	const auto fire = [&](s32 kind, u32 passing) {
		mission_graph_event_t event = {};
		event.kind = kind;
		event.instance = &instance;
		Choice choice;
		choice.passing = passing;
		CHECK(missionGraphProgramFire(program, &event, runModule, &choice) >= 0);
		return choice;
	};
	const u32 close = PROJECTILE_RULE_GATE_TRIGGER_CLOSE;
	const u32 character = PROJECTILE_RULE_GATE_TRIGGER_CHARACTER;
	const u32 direct = PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST;
	// The grenade: only a character within 250 units.
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, close | character).modules == PROJECTILE_MODULE_SPEND_FUSE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, close).modules == 0);     // a moving object
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, character).modules == 0); // in the outer ring
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, 0).modules == 0);
	// The homing rocket: a shot, not a blast.
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED, direct).modules == PROJECTILE_MODULE_SPEND_FUSE);
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED, 0).modules == 0);
	// The remote mine: its owner's detonator, no gate.
	CHECK(fire(MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, 0).modules == PROJECTILE_MODULE_SPEND_FUSE);
	// The fuse: an explosion, the one the node names.
	const Choice fuse = fire(MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, 0);
	CHECK(fuse.modules == PROJECTILE_MODULE_EXPLODE);
	CHECK(fuse.explosion == EXPLOSIONTYPE_DRAGONBOMBSPY);
	missionGraphProgramFree(program);

	// An explode that names none is the object's own; a storm is a module of its own.
	const std::string storm = R"({
  "schema": "pd.projectile_graph.v2",
  "nodes": [
    { "id": "fuse", "kind": "event.projectile.timer_expired" },
    { "id": "fuse_nbomb_storm", "kind": "og.projectile.nbomb_storm" },
    { "id": "fuse_explode", "kind": "og.projectile.explode" }
  ],
  "edges": [
    { "from": "fuse", "output": "out", "to": "fuse_nbomb_storm", "input": "exec" },
    { "from": "fuse", "output": "out", "to": "fuse_explode", "input": "exec" }
  ]
})";
	program = missionGraphProgramCompileProjectile(storm.data(), storm.size(), moduleKnown, err, sizeof(err));
	INFO(err);
	REQUIRE(program != nullptr);
	mission_graph_event_t event = {};
	event.kind = MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED;
	event.instance = &instance;
	Choice both;
	CHECK(missionGraphProgramFire(program, &event, runModule, &both) >= 0);
	CHECK(both.modules == (PROJECTILE_MODULE_EXPLODE | PROJECTILE_MODULE_NBOMB_STORM));
	CHECK(both.explosion == PROJECTILE_EXPLOSION_OWN);
	missionGraphProgramFree(program);
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
	REQUIRE(propobj.find("static u32 projectileRuleWith(s32 kind, struct defaultobj *obj,\n"
		"\t\tconst projectile_rule_context_t *context)\n{") != std::string::npos);
	REQUIRE(propobj.find("\tnative = projectileRulesNative(kind, weapon->weaponnum, weapon->gunfunc, sticks);\n"
		"\treturn projectileRuleModulesWith(kind, obj, native, context);\n}") != std::string::npos);
	// Only a contact asks the flying object's function for its stick flag (a
	// shot pickup may have no function).
	REQUIRE(propobj.find("\tif (kind == MISSION_GRAPH_EVENT_PROJECTILE_CONTACT) {") != std::string::npos);
	REQUIRE(countOf(propobj, "gsetHasFunctionFlags(&weapon->gset, FUNCFLAG_STICKTOWALL)") == 1);
	REQUIRE(propobj.find("static u32 projectileRuleFor(s32 kind, struct defaultobj *obj)\n{\n"
		"\treturn projectileRuleWith(kind, obj, NULL);\n}") != std::string::npos);
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
	// dropped, the host counts parity. The original's gates and a program's
	// gate nodes are answered by one function.
	REQUIRE(events.find("u32 projectileRuleModules(s32 kind, struct defaultobj *obj, u32 native)\n{\n"
		"\treturn projectileRuleModulesWith(kind, obj, native, NULL);\n}") != std::string::npos);
	REQUIRE(events.find("u32 projectileRuleModulesWith(s32 kind, struct defaultobj *obj, u32 native,\n"
		"\t\tconst projectile_rule_context_t *context)\n{\n"
		"\tconst u32 original = projectileNativeChoice(native, obj, context);") != std::string::npos);
	REQUIRE(events.find("\t\treturn original; /* a v1 archive: the original rules decide */") != std::string::npos);
	REQUIRE(events.find("\t\trule.modules &= projectileRulesEventModules(kind);") != std::string::npos);
	REQUIRE(events.find("\tif (g_NetMode != NETMODE_CLIENT && (handled || original)) {") != std::string::npos);
	REQUIRE(events.find("\"PROJECTILE.RULE differs: %s weapon=%s func=%s program=%s modules=%s native=%s\"") != std::string::npos);
	REQUIRE(events.find(" fuse.compared=%d fuse.differ=%d rules.compared=%d rules.differ=%d") != std::string::npos);
	REQUIRE(events.find("static s32 projectileGatePasses(u32 gate, const struct defaultobj *obj,\n"
		"\t\tconst projectile_rule_context_t *context)\n{") != std::string::npos);
	REQUIRE(events.find("\t\treturn projectileGatePasses(gate, rule->obj, rule->context) ? 1 : 0;") != std::string::npos);
	REQUIRE(events.find("\t\tif ((native & gate) && !projectileGatePasses(gate, obj, context)) {\n"
		"\t\t\treturn 0;") != std::string::npos);
	REQUIRE(events.find("\t\treturn context && context->damage_weaponnum != WEAPON_REMOTEMINE;") != std::string::npos);

	// Registration takes the table's modules and gates and refuses a module
	// wired where it would do nothing, a gate wired where it cannot be
	// answered, and an explode that names no explosion of the table.
	REQUIRE(runtime.find("\treturn projectileRulesModuleBit(kind) != 0 ||\n"
		"\t\tprojectileRulesGateBit(kind) != 0;") != std::string::npos);
	REQUIRE(runtime.find("snprintf(why, why_cap, \"%s has no effect on %s\", module_kind,") != std::string::npos);
	REQUIRE(runtime.find("snprintf(why, why_cap, \"%s cannot be answered at %s\", gate_kind,") != std::string::npos);
	REQUIRE(runtime.find("\tif (!missionGraphProgramVisitModuleParams(program, projectileProgramExplosionVisit, NULL)) {")
		!= std::string::npos);
	REQUIRE(runtime.find("\tif (!projectileProgramModulesApply(program, is_projectile, why, sizeof(why))) {")
		!= std::string::npos);

	// The extractor writes the wiring from the table, for the object the
	// function launches, behind the weapon cache kind.
	REQUIRE(extractor.find("static s32 s_projectileRuleWiring(const pdweapon_nested_payload_t *p, s32 fuse_only,")
		!= std::string::npos);
	REQUIRE(extractor.find("\t\t\tmodules = projectileRulesNative(events[i].kind, objweapon, objfunc, sticks);") != std::string::npos);
	// Slice 6: the object's arm is wired first, since only an arm asks the fuse rule.
	REQUIRE(extractor.find("\t} events[] = {\n\t\t{ MISSION_GRAPH_EVENT_PROJECTILE_ARMED, \"armed\" },\n"
		"\t\t{ MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, \"fuse\" },") != std::string::npos);
	// Slice 5: the throw's own rule is by the thrower's weapon, and the
	// sentry's rule goes only to a throw that deploys one.
	REQUIRE(extractor.find("\tthrown = f->type == INVENTORYFUNCTYPE_THROW\n"
		"\t\t? projectileRulesNative(MISSION_GRAPH_EVENT_PROJECTILE_THROWN, p->weaponnum, p->mode_index, 0)\n"
		"\t\t: 0;") != std::string::npos);
	REQUIRE(extractor.find("\tif (!has_object && !(thrown & PROJECTILE_MODULE_ALL)) return 0;") != std::string::npos);
	REQUIRE(extractor.find("\t\t\tif (thrown & PROJECTILE_MODULE_DEPLOY_SENTRY) {\n"
		"\t\t\t\tmodules = projectileRulesNative(events[i].kind, p->weaponnum, p->mode_index, 0);") != std::string::npos);
	REQUIRE(extractor.find("const s32 explosion = projectileRulesNativeExplosion(objweapon, objfunc);") != std::string::npos);
	REQUIRE(extractor.find("\tprojectileRulesLaunchedObject(p->weaponnum, p->mode_index,") != std::string::npos);
	REQUIRE(extractor.find("\t\tpayload_plan.payloads[i].weaponnum = weapon_id;") != std::string::npos);
	REQUIRE(extractor.find("_projref_fuseprogram_contactrules_devicerules_sentryrules_armrules\"") != std::string::npos);
	// Every rule event of the table is wired, the arm and the fuse first.
	for (const char *row : { "{ MISSION_GRAPH_EVENT_PROJECTILE_ARMED, \"armed\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, \"fuse\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, \"contact\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, \"launch_blocked\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, \"bounced\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, \"came_to_rest\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_LANDED, \"landed\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, \"proximity\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, \"remote_signal\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED, \"damaged\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_THROWN, \"thrown\" },",
			"{ MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED, \"sentry_aimed\" }," }) {
		INFO(row);
		CHECK(extractor.find(row) != std::string::npos);
	}
	// A projectile archive carries all of its object's rules; an entity
	// archive its fuse alone.
	CHECK(extractor.find("\tif (s_projectileRuleWiring(p, 0, rule_nodes, sizeof(rule_nodes),") != std::string::npos);
	CHECK(extractor.find("\tif (s_projectileRuleWiring(p, 1, rule_nodes, sizeof(rule_nodes),") != std::string::npos);

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
	for (s32 g = 0; g < PROJECTILE_GATE_COUNT; g++) {
		const std::string kind = projectileRulesGateKind(PROJECTILE_RULE_GATE_FUSE_ARMED << g);
		INFO(kind);
		CHECK(conformance.find("\"" + kind + "\"") != std::string::npos);
	}
	CHECK(conformance.find("PROJECTILE_GRAPH_GATES = (\"gate.objective.status\", \"gate.mission.phase\",\n"
		"                          \"gate.projectile.fuse_armed\", \"gate.projectile.trigger_close\",\n"
		"                          \"gate.projectile.trigger_character\",\n"
		"                          \"gate.projectile.damage_not_blast\")") != std::string::npos);
	CHECK(conformance.find("PROJECTILE_GRAPH_EVENT_MODULES = {") != std::string::npos);
	CHECK(conformance.find("PROJECTILE_GRAPH_GATE_EVENTS = {") != std::string::npos);
	CHECK(conformance.find("PROJECTILE_GRAPH_EXPLOSION_TYPES = 26\n") != std::string::npos);
}

TEST_CASE("projectile rules: the fuses, the mines and damage ask the rules", "[projectile][rules][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string events = readSource("port/src/projectile_events.c");

	// weaponTick: every fuse asks what goes off, and what goes off is the
	// modules' choice, with no weapon number or function asked.
	const size_t tick = propobj.find("void weaponTick(struct prop *prop)\n{");
	REQUIRE(tick != std::string::npos);
	const size_t tickEnd = propobj.find("\n}\n", tick);
	REQUIRE(tickEnd != std::string::npos);
	const std::string body = propobj.substr(tick, tickEnd - tick);
	// One per arm that runs a fuse (slice 6: the countdown, the struck fuse,
	// the mine's, the remote mine's, the proximity item's).
	CHECK(countOf(body, "(fusemods = projectileFuseFor(obj, &fusetype)) != 0") == 5);
	// One per fuse, and two ways to tidy up for the countdown (a storm, an
	// explosion) and for the proximity items.
	CHECK(countOf(body, "weaponGoOffAs(prop, fusemods, fusetype)") == 7);
	CHECK(body.find("EXPLOSIONTYPE_SDGRENADE") == std::string::npos);
	CHECK(body.find("EXPLOSIONTYPE_DRAGONBOMBSPY") == std::string::npos);
	CHECK(body.find("if (weapon->gunfunc == FUNC_2) {") == std::string::npos);
	CHECK(body.find("if (weapon->weaponnum == WEAPON_NBOMB) {") == std::string::npos);
	CHECK(body.find("if (weapon->weaponnum == WEAPON_DRAGON) {") == std::string::npos);
	CHECK(body.find("\t\t\tif (fusemods & PROJECTILE_MODULE_NBOMB_STORM) {\n"
		"\t\t\t\tweaponGoOffAs(prop, fusemods, fusetype);\n"
		"\t\t\t\tpropUnsetDangerous(prop);") != std::string::npos);
	CHECK(body.find("\t\t\t} else if (weaponGoOffAs(prop, fusemods, fusetype)) {\n"
		"\t\t\t\t// Regular explosive\n"
		"\t\t\t\tweapon->timer240 = -1;") != std::string::npos);
	// The original fuse arms outside the custom ones never set off a storm or
	// an explosion themselves any more.
	CHECK(countOf(propobj, "nbombCreateStorm_hack(&prop->pos, ownerprop, prop);") == 2); // the helper, the custom arm
	REQUIRE(propobj.find("static bool weaponGoOffAs(struct prop *prop, u32 fusemods, s32 exptype)\n{") != std::string::npos);
	REQUIRE(propobj.find("\tif (fusemods & PROJECTILE_MODULE_EXPLODE) {\n"
		"\t\tif (exptype < 0) {\n"
		"\t\t\texptype = (obj->flags2 & OBJFLAG2_WEAPON_HUGEEXP) ? EXPLOSIONTYPE_HUGE17 : EXPLOSIONTYPE_ROCKET;\n"
		"\t\t}") != std::string::npos);
	REQUIRE(propobj.find("static u32 projectileFuseFor(struct defaultobj *obj, s32 *exptype)\n{") != std::string::npos);
	REQUIRE(propobj.find("\t\tprojectileRulesNativeExplosion(weapon->weaponnum, weapon->gunfunc), exptype);") != std::string::npos);

	// The remote mine: the owner test stays the game's; the rule says
	// whether the signal spends the fuse.
	CHECK(countOf(body, "signalled = true;") == 3);
	CHECK(body.find("\t\t\t\tif (signalled && (projectileRuleFor(MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, obj)\n"
		"\t\t\t\t\t\t& PROJECTILE_MODULE_SPEND_FUSE)) {\n"
		"\t\t\t\t\tweapon->timer240 = 0;") != std::string::npos);
	// The proximity item's own test of the player of the tick.
	CHECK(body.find("\t\t\t\tif (projectileRuleWith(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, obj, &nearby)\n"
		"\t\t\t\t\t\t& PROJECTILE_MODULE_SPEND_FUSE) {\n"
		"\t\t\t\t\tweapon->timer240 = 0;") != std::string::npos);

	// The watch list: no weapon number decides for a base weapon; the rule
	// is asked for anything within twice the area of the 250 unit circle,
	// and told whether it is within 250 units and whether it is a character.
	const size_t sweep = propobj.find("static void coordTriggerProxiesFor(struct chrdata *chr, struct coord *pos, bool arg1)\n{");
	REQUIRE(sweep != std::string::npos);
	const std::string watch = propobj.substr(sweep, propobj.find("\n}\n", sweep) - sweep);
	CHECK(watch.find("WEAPON_DRAGON") == std::string::npos);
	CHECK(watch.find("WEAPON_GRENADE") == std::string::npos);
	CHECK(watch.find("\t\t\t} else if (distsq < range + range) {") != std::string::npos);
	CHECK(watch.find("\t\t\t\tnearby.trigger_close = distsq < range;\n"
		"\t\t\t\tnearby.trigger_character = arg1 == true;") != std::string::npos);
	CHECK(watch.find("\t\t\t\tif (projectileRuleWith(MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, &weapon->base, &nearby)\n"
		"\t\t\t\t\t\t& PROJECTILE_MODULE_SPEND_FUSE) {\n"
		"\t\t\t\t\tweapon->timer240 = 0;") != std::string::npos);
	// A mod weapon whose record makes no decision keeps the plain rule.
	CHECK(watch.find("\t\t\tif (weapon->weaponnum >= WEAPON_CUSTOM_START) {\n"
		"\t\t\t\t/* A mod weapon whose record makes no decision: the plain\n"
		"\t\t\t\t * rule, 250 units and anything that moves. */\n"
		"\t\t\t\tif (distsq < range) {\n"
		"\t\t\t\t\tweapon->timer240 = 0;") != std::string::npos);

	// Damage taken: the list of explosives is the rule's, and so is the
	// homing rocket's exemption.
	const size_t damage = propobj.find("void objDamage(struct defaultobj *obj, f32 damage, struct coord *pos, s32 weaponnum, s32 playernum)\n{");
	REQUIRE(damage != std::string::npos);
	const std::string taken = propobj.substr(damage, propobj.find("\n}\n", damage) - damage);
	CHECK(taken.find("weapon->weaponnum == WEAPON_GRENADE") == std::string::npos);
	CHECK(taken.find("WEAPON_HOMINGROCKET") == std::string::npos);
	CHECK(taken.find("\t\t\tdamagedby.damage_weaponnum = weaponnum;\n\n"
		"\t\t\tif (projectileRuleWith(MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED, obj, &damagedby)\n"
		"\t\t\t\t\t& PROJECTILE_MODULE_SPEND_FUSE) {\n"
		"\t\t\t\tweapon->timer240 = 0;\n"
		"\t\t\t} else if (weapon->weaponnum >= WEAPON_CUSTOM_START) {") != std::string::npos);

	// The fuse rule: the program's modules, the explosion its explode names,
	// the original for a v1 archive and for a networked client.
	REQUIRE(events.find("u32 projectileFuseModules(struct defaultobj *obj, u32 native, s32 native_explosion,\n"
		"\t\ts32 *explosion)\n{") != std::string::npos);
	REQUIRE(events.find("\tif (!obj || obj->type != OBJTYPE_WEAPON || g_NetMode == NETMODE_CLIENT\n"
		"\t\t\t|| !projectileIdentify(obj, &who)) {\n\t\treturn original;") != std::string::npos);
	REQUIRE(events.find("\t\treturn original; /* a v1 archive: the original fuse decides */") != std::string::npos);
	REQUIRE(events.find("\tdiffers = rule.modules != original\n"
		"\t\t|| ((original & PROJECTILE_MODULE_EXPLODE) && rule.explosion != native_explosion);") != std::string::npos);
	REQUIRE(events.find("\t\tif (bit == PROJECTILE_MODULE_EXPLODE) {") != std::string::npos);
	REQUIRE(events.find("missionGraphParamText(params, PROJECTILE_EXPLODE_TYPE_PARAM));") != std::string::npos);
	REQUIRE(events.find("projectileFuseRule") == std::string::npos);
}

TEST_CASE("projectile rules: weaponTick runs the arm each object chose", "[projectile][rules][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string events = readSource("port/src/projectile_events.c");
	const std::string runtime = readSource("port/src/weapon_graph_runtime.c");
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	// One helper asks: the original's arm goes in as the answer for an object
	// with no program and as the parity reference.
	REQUIRE(propobj.find("static u32 projectileArmsFor(struct defaultobj *obj)\n{\n"
		"\tstruct weaponobj *weapon = (struct weaponobj *)obj;\n\n"
		"\treturn projectileArmModules(obj,\n"
		"\t\tprojectileRulesNative(MISSION_GRAPH_EVENT_PROJECTILE_ARMED,\n"
		"\t\t\tweapon->weaponnum, weapon->gunfunc, 0));\n}") != std::string::npos);

	const size_t tick = propobj.find("void weaponTick(struct prop *prop)\n{");
	REQUIRE(tick != std::string::npos);
	const size_t tickEnd = propobj.find("\n}\n", tick);
	REQUIRE(tickEnd != std::string::npos);
	const std::string body = propobj.substr(tick, tickEnd - tick);
	CHECK(countOf(body, "projectileArmsFor(obj)") == 1);
	CHECK(body.find("\tconst u32 arms = projectileArmsFor(obj);") != std::string::npos);
	// No weapon number picks an arm any more. The two tests that are left
	// guard a mod weapon's records (the owner cleanup, the record entity arm).
	CHECK(body.find("weaponnum == WEAPON_") == std::string::npos);
	CHECK(countOf(body, "weapon->weaponnum >= WEAPON_CUSTOM_START") == 2);
	// The arms, in the order the original tested them, with a mod weapon's
	// two record arms where they have always stood.
	const char *chain[] = {
		"\tif ((arms & (PROJECTILE_MODULE_ARM_FUSE | PROJECTILE_MODULE_ARM_WALL_FUSE))\n"
			"\t\t\t&& weapon->timer240 >= 0) {",
		"\t\tif ((arms & PROJECTILE_MODULE_ARM_WALL_FUSE) && weapon->timer240 > 0) {",
		"\t} else if (arms & PROJECTILE_MODULE_ARM_STRUCK_FUSE) {",
		"\t} else if (customproj != NULL\n",
		"\t} else if ((arms & PROJECTILE_MODULE_ARM_MINE_FUSE) && weapon->timer240 >= 0) {",
		"\t} else if (arms & PROJECTILE_MODULE_ARM_REMOTE) {",
		"\t} else if (arms & PROJECTILE_MODULE_ARM_PROXIMITY) {",
		"\t} else if (weapon->weaponnum >= WEAPON_CUSTOM_START && !(arms & PROJECTILE_ARMS_PROGRAM)\n"
			"\t\t\t&& entitygraph\n",
		"\t} else if (arms & PROJECTILE_MODULE_ARM_BOLT) {",
	};
	size_t at = 0;
	for (const char *entry : chain) {
		INFO(entry);
		const size_t found = body.find(entry, at);
		REQUIRE(found != std::string::npos);
		CHECK(countOf(body, entry) == 1);
		at = found + 1;
	}
	// The timed mine's arm asks no function: the arm rule gives the arm to
	// its first function.
	CHECK(body.find("\t\tif (weapon->gunfunc == FUNC_PRIMARY) {\n\t\t\tweapon->timer240 -= g_Vars.lvupdate240;")
		== std::string::npos);
	CHECK(body.find("\t\t// Handle timed mines\n") != std::string::npos);
	// The countdown arm keeps each original's order of tidying up: the
	// N-bomb's storm starts before it leaves the dangerous list, a grenade
	// leaves it before it explodes.
	CHECK(body.find("\t\t\t\tif (fusemods & PROJECTILE_MODULE_NBOMB_STORM) {\n"
		"\t\t\t\t\t// Nbombs detonate when they hit the ground, so this code only\n"
		"\t\t\t\t\t// runs if it's airborne for the entire duration of its timer.\n"
		"\t\t\t\t\tweaponGoOffAs(prop, fusemods, fusetype);\n"
		"\t\t\t\t\tpropUnsetDangerous(prop);\n"
		"\t\t\t\t} else {\n"
		"\t\t\t\t\tpropUnsetDangerous(prop);\n\n"
		"\t\t\t\t\tweaponGoOffAs(prop, fusemods, fusetype);\n"
		"\t\t\t\t}") != std::string::npos);
	// An object whose archive has a program runs none of a mod weapon's
	// record arms.
	CHECK(body.find("\tconst weapon_graph_projectile_runtime_t *customproj =\n"
		"\t\t(arms & PROJECTILE_ARMS_PROGRAM) ? NULL : weaponGetCustomProjectileGraph(weapon);")
		!= std::string::npos);

	// The choice is kept in the prop's record (found by the prop's place in
	// the stage's array, valid for its generation), for the weapon number and
	// function it was given for. A change of either asks again, because the
	// original read both every tick and a simulant switching function
	// rewrites the function of the weapon it holds.
	REQUIRE(events.find("u32 projectileArmModules(struct defaultobj *obj, u32 native)\n{\n"
		"\tconst u32 original = projectileNativeChoice(native, obj, NULL);") != std::string::npos);
	REQUIRE(events.find("\tkept = projectileRecordOf(obj->prop, 1);\n"
		"\tif (kept && kept->armed && kept->arm_weaponnum == weapon->weaponnum\n"
		"\t\t\t&& kept->arm_gunfunc == (s8)weapon->gunfunc) {\n"
		"\t\treturn kept->arms;\n\t}") != std::string::npos);
	REQUIRE(events.find("\tif (projectileIdentify(obj, &who)\n"
		"\t\t\t&& projectileProgramFor(&who, MISSION_GRAPH_EVENT_PROJECTILE_ARMED, &asset_id)) {") != std::string::npos);
	REQUIRE(events.find("\t\tarms = projectileRuleDecide(MISSION_GRAPH_EVENT_PROJECTILE_ARMED, obj, obj, who,\n"
		"\t\t\toriginal, NULL) | PROJECTILE_ARMS_PROGRAM;") != std::string::npos);
	REQUIRE(events.find("\tindex = (s32)(prop - g_Vars.props);") != std::string::npos);
	REQUIRE(events.find("\t\tkept->armed = 1;\n"
		"\t\tkept->arm_weaponnum = (s16)weapon->weaponnum;\n"
		"\t\tkept->arm_gunfunc = (s8)weapon->gunfunc;\n"
		"\t\tkept->arms = arms;") != std::string::npos);
	// A launch that binds the object after it chose makes it choose again.
	REQUIRE(events.find("\tbind->armed = 0; /* its archive is known now: it chooses its arm again */")
		!= std::string::npos);
	// The parity probe counts where the original has a rule to compare with:
	// a mod weapon's number has none, so its choices are logged, not counted.
	REQUIRE(events.find("\t\tconst s32 counted = who.gunnum < WEAPON_CUSTOM_START;\n\n"
		"\t\ts_ProjectileRuleCompared += counted;\n"
		"\t\tif (counted && rule.modules != original) {") != std::string::npos);
	REQUIRE(events.find("\tif (who.gunnum >= WEAPON_CUSTOM_START) {\n\t\tdiffers = 0;\n\t} else {\n"
		"\t\ts_ProjectileFuseCompared++;\n\t}") != std::string::npos);

	// The watch list: an object joins it through the proximity arm, which a
	// program can choose for any weapon object, so every weapon object leaves
	// it when freed. No weapon number is asked.
	const size_t freeAt = propobj.find("void objFree(struct defaultobj *obj, bool freeprop, bool canregen)\n{");
	REQUIRE(freeAt != std::string::npos);
	const std::string freed = propobj.substr(freeAt, propobj.find("\n}\n", freeAt) - freeAt);
	CHECK(countOf(freed, "weaponUnregisterProxy(weapon);") == 1);
	CHECK(freed.find("\t\tweaponUnregisterProxy(weapon);\n\n"
		"\t\tif (weapon->weaponnum == WEAPON_GRENADE && weapon->gunfunc == FUNC_SECONDARY) {\n"
		"\t\t\tsmokeClearForProp(obj->prop);") != std::string::npos);
	CHECK(freed.find("weapon->weaponnum == WEAPON_PROXIMITYMINE") == std::string::npos);
	CHECK(countOf(body, "weaponRegisterProxy(weapon);") == 2); // the proximity arm, a mod weapon's record arm

	// Registration: a projectile archive's program that wires a fuse must
	// arm it; an entity archive carries its item's fuse alone.
	REQUIRE(runtime.find("\"event.projectile.armed reaches no arm that runs a fuse\"") != std::string::npos);
	REQUIRE(runtime.find("\t\t\tif ((bit & PROJECTILE_MODULE_ARMS) && bit != PROJECTILE_MODULE_ARM_BOLT) {")
		!= std::string::npos);
	REQUIRE(runtime.find("\tif (projectileProgramRegister(ir, 1, err, err_cap) != 0) {\n"
		"\t\tif (index >= 0) projectileRuntimeClearSlot(index);") != std::string::npos);
	REQUIRE(runtime.find("\tif (projectileProgramRegister(ir, 0, err, err_cap) != 0) {\n"
		"\t\tif (index >= 0) entityRuntimeClearSlot(index);") != std::string::npos);

	// The validator: the same arms run a fuse, and the same refusal.
	CHECK(conformance.find("PROJECTILE_GRAPH_FUSE_ARMS = (\"og.projectile.arm_fuse\", \"og.projectile.arm_wall_fuse\",\n")
		!= std::string::npos);
	CHECK(conformance.find("reaches no arm that runs a fuse") != std::string::npos);
	CHECK(conformance.find("return validate_projectile_graph_program(where, graph, entity=ext == \".pdentity\")")
		!= std::string::npos);
}

TEST_CASE("projectile rules: a throw and a sentry ask the rules", "[projectile][rules][static]")
{
	const std::string propobj = readSource("src/game/propobj.c");
	const std::string bgun = readSource("src/game/bondgun.c");
	const std::string events = readSource("port/src/projectile_events.c");

	// The throw: the rule says whether it makes a sentry; no weapon number
	// is asked. A mod weapon's v1 record still says so with its transition.
	REQUIRE(bgun.find("\t\tif ((projectileThrowModules(gset, projectilegraph ? projectilegraph->asset_id : NULL,\n"
		"\t\t\t\t\tprojectileRulesNative(MISSION_GRAPH_EVENT_PROJECTILE_THROWN,\n"
		"\t\t\t\t\t\tgset->weaponnum, gset->weaponfunc, 0))\n"
		"\t\t\t\t\t& PROJECTILE_MODULE_DEPLOY_SENTRY)\n"
		"\t\t\t\t|| transitionentity != NULL) {\n"
		"\t\t\tautogun = laptopDeploy(projectilemodelnum, gset, chr);") != std::string::npos);
	REQUIRE(bgun.find("gset->weaponnum == WEAPON_LAPTOPGUN || transitionentity != NULL") == std::string::npos);

	// The sentry keeps the archive it was thrown from with its deploy record,
	// because the launch's binding does not last as long as a sentry does.
	REQUIRE(propobj.find("\ts32 throwweaponnum;\n\ts32 throwfunc;\n\tchar programasset[CATALOG_ID_LEN];\n};") != std::string::npos);
	REQUIRE(propobj.find("\t\t\t\tif (gset && projectilerec && projectilerec->asset_id[0]) {\n"
		"\t\t\t\t\tlatch->throwweaponnum = gset->weaponnum;\n"
		"\t\t\t\t\tlatch->throwfunc = gset->weaponfunc;") != std::string::npos);
	REQUIRE(propobj.find("static u32 autogunSentryRule(s32 kind, struct defaultobj *obj)\n{") != std::string::npos);
	REQUIRE(propobj.find("\t\tif (latch != NULL && latch->programasset[0]) {\n"
		"\t\t\treturn projectileSentryModules(kind, obj, latch->throwweaponnum, latch->throwfunc,\n"
		"\t\t\t\tlatch->programasset, native);") != std::string::npos);
	// A mission's own turret has no archive: the original answers.
	REQUIRE(propobj.find("\treturn native & PROJECTILE_MODULE_ALL;\n}\n\nvoid thrownLaptopLatchResetAll(void)") != std::string::npos);

	// The sentry: aimed and about to fire, it asks the rule, on the host
	// (autogunTick returns at once on a network client).
	const size_t tick = propobj.find("void autogunTick(struct prop *prop)\n{");
	REQUIRE(tick != std::string::npos);
	const std::string body = propobj.substr(tick, propobj.find("\n}\n", tick) - tick);
	CHECK(body.find("\tif (g_NetMode == NETMODE_CLIENT) {\n\t\treturn;\n\t}") != std::string::npos);
	CHECK(body.find("\tif (autogun->firing\n"
		"\t\t\t&& !(autogunSentryRule(MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED, obj)\n"
		"\t\t\t\t& PROJECTILE_MODULE_SENTRY_FIRE)) {\n"
		"\t\tautogun->firing = false;\n\t}") != std::string::npos);

	// Both rules name their archive themselves and go through the one
	// decision the object rules use.
	REQUIRE(events.find("u32 projectileThrowModules(const struct gset *gset, const char *asset_id, u32 native)\n{") != std::string::npos);
	REQUIRE(events.find("\treturn projectileRuleDecide(MISSION_GRAPH_EVENT_PROJECTILE_THROWN, NULL, (void *)gset, who,\n"
		"\t\toriginal, NULL);") != std::string::npos);
	REQUIRE(events.find("u32 projectileSentryModules(s32 kind, struct defaultobj *obj, s32 gunnum, s32 func,\n"
		"\t\tconst char *asset_id, u32 native)\n{") != std::string::npos);
	REQUIRE(countOf(events, "return projectileRuleDecide(") == 3);
	REQUIRE(events.find("\tif (!gset || !asset_id || !asset_id[0]) {\n\t\treturn original;\n\t}") != std::string::npos);
}
