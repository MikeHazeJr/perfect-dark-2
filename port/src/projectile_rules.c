/*
 * projectile_rules.c -- the modules of a projectile program and the original
 * game's rules by weapon number: see projectile_rules.h and
 * context/designs/modding/projectile-events.md (slices 3 and 4).
 */

#include <string.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "mission_graph_runner.h"
#include "projectile_rules.h"

static const struct {
	u32 bit;
	const char *kind;
} s_ProjectileModules[PROJECTILE_MODULE_COUNT] = {
	{ PROJECTILE_MODULE_EXPLODE, "og.projectile.explode" },
	{ PROJECTILE_MODULE_STICK, "og.projectile.stick" },
	{ PROJECTILE_MODULE_WALL_HUG, "og.projectile.wall_hug" },
	{ PROJECTILE_MODULE_PIERCE, "og.projectile.pierce" },
	{ PROJECTILE_MODULE_IMPACT, "og.projectile.impact" },
	{ PROJECTILE_MODULE_SPEND_FUSE, "og.projectile.spend_fuse" },
	{ PROJECTILE_MODULE_SHIELD_DEVICE, "og.projectile.shield_device" },
	{ PROJECTILE_MODULE_LAND_BOLT, "og.projectile.land_bolt" },
	{ PROJECTILE_MODULE_LAND_KNIFE, "og.projectile.land_knife" },
	{ PROJECTILE_MODULE_POISON_TARGET, "og.projectile.poison_target" },
	{ PROJECTILE_MODULE_NBOMB_STORM, "og.projectile.nbomb_storm" },
	{ PROJECTILE_MODULE_DEPLOY_SENTRY, "og.projectile.deploy_sentry" },
	{ PROJECTILE_MODULE_SENTRY_FIRE, "og.projectile.sentry_fire" },
};

static const struct {
	u32 bit;
	const char *kind;
} s_ProjectileGates[PROJECTILE_GATE_COUNT] = {
	{ PROJECTILE_RULE_GATE_FUSE_ARMED, PROJECTILE_GATE_FUSE_ARMED_KIND },
	{ PROJECTILE_RULE_GATE_TRIGGER_CLOSE, PROJECTILE_GATE_TRIGGER_CLOSE_KIND },
	{ PROJECTILE_RULE_GATE_TRIGGER_CHARACTER, PROJECTILE_GATE_TRIGGER_CHARACTER_KIND },
	{ PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST, PROJECTILE_GATE_DAMAGE_NOT_BLAST_KIND },
};

/* Every projectile event, in kind order, with the modules its seam applies. */
static const struct {
	s32 kind;
	const char *name;
	u32 modules;
} s_ProjectileEvents[] = {
	{ MISSION_GRAPH_EVENT_PROJECTILE_SPAWNED, "event.projectile.spawned", 0 },
	{ MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED, "event.projectile.bounced", PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_HIT_TARGET, "event.projectile.hit_target", 0 },
	{ MISSION_GRAPH_EVENT_PROJECTILE_LANDED, "event.projectile.landed",
		PROJECTILE_MODULE_SHIELD_DEVICE | PROJECTILE_MODULE_LAND_BOLT | PROJECTILE_MODULE_LAND_KNIFE
			| PROJECTILE_MODULE_POISON_TARGET },
	{ MISSION_GRAPH_EVENT_PROJECTILE_ENDED, "event.projectile.ended", 0 },
	{ MISSION_GRAPH_EVENT_PROJECTILE_DETONATED, "event.projectile.detonated", 0 },
	{ MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED, "event.projectile.timer_expired",
		PROJECTILE_MODULE_EXPLODE | PROJECTILE_MODULE_NBOMB_STORM },
	{ MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, "event.projectile.contact",
		PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_WALL_HUG | PROJECTILE_MODULE_PIERCE
			| PROJECTILE_MODULE_IMPACT },
	{ MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, "event.projectile.launch_blocked",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, "event.projectile.came_to_rest",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY, "event.projectile.proximity",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL, "event.projectile.remote_signal",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED, "event.projectile.damaged",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_THROWN, "event.projectile.thrown",
		PROJECTILE_MODULE_DEPLOY_SENTRY },
	{ MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED, "event.projectile.sentry_aimed",
		PROJECTILE_MODULE_SENTRY_FIRE },
};
#define PROJECTILE_EVENT_ROWS ((s32)(sizeof(s_ProjectileEvents) / sizeof(s_ProjectileEvents[0])))

const char *projectileRulesModuleKind(u32 bit)
{
	s32 i;

	for (i = 0; i < PROJECTILE_MODULE_COUNT; i++) {
		if (s_ProjectileModules[i].bit == bit) {
			return s_ProjectileModules[i].kind;
		}
	}
	return NULL;
}

u32 projectileRulesModuleBit(const char *kind)
{
	s32 i;

	if (!kind) {
		return 0;
	}
	for (i = 0; i < PROJECTILE_MODULE_COUNT; i++) {
		if (strcmp(s_ProjectileModules[i].kind, kind) == 0) {
			return s_ProjectileModules[i].bit;
		}
	}
	return 0;
}

const char *projectileRulesGateKind(u32 gate)
{
	s32 i;

	for (i = 0; i < PROJECTILE_GATE_COUNT; i++) {
		if (s_ProjectileGates[i].bit == gate) {
			return s_ProjectileGates[i].kind;
		}
	}
	return NULL;
}

u32 projectileRulesGateBit(const char *kind)
{
	s32 i;

	if (!kind) {
		return 0;
	}
	for (i = 0; i < PROJECTILE_GATE_COUNT; i++) {
		if (strcmp(s_ProjectileGates[i].kind, kind) == 0) {
			return s_ProjectileGates[i].bit;
		}
	}
	return 0;
}

s32 projectileRulesGateApplies(u32 gate, s32 event_kind)
{
	switch (gate) {
	case PROJECTILE_RULE_GATE_FUSE_ARMED:
		/* It reads the projectile itself. */
		return projectileRulesEventName(event_kind) != NULL;
	case PROJECTILE_RULE_GATE_TRIGGER_CLOSE:
	case PROJECTILE_RULE_GATE_TRIGGER_CHARACTER:
		return event_kind == MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY;
	case PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST:
		return event_kind == MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED;
	default:
		return 0;
	}
}

s32 projectileRulesEventAt(s32 index)
{
	return index >= 0 && index < PROJECTILE_EVENT_ROWS ? s_ProjectileEvents[index].kind : -1;
}

const char *projectileRulesEventName(s32 event_kind)
{
	s32 i;

	for (i = 0; i < PROJECTILE_EVENT_ROWS; i++) {
		if (s_ProjectileEvents[i].kind == event_kind) {
			return s_ProjectileEvents[i].name;
		}
	}
	return NULL;
}

u32 projectileRulesEventModules(s32 event_kind)
{
	s32 i;

	for (i = 0; i < PROJECTILE_EVENT_ROWS; i++) {
		if (s_ProjectileEvents[i].kind == event_kind) {
			return s_ProjectileEvents[i].modules;
		}
	}
	return 0;
}

u32 projectileRulesNative(s32 event_kind, s32 weaponnum, s32 gunfunc, s32 sticks_to_wall)
{
	u32 modules = 0;

	switch (event_kind) {
	case MISSION_GRAPH_EVENT_PROJECTILE_TIMER_EXPIRED:
		/* weaponTick's fuses, for the objects that have one: the N-bomb
		 * becomes a storm, the rest explode. A timed mine's fuse counts for
		 * its first function only. */
		if (weaponnum == WEAPON_NBOMB
				&& (gunfunc == FUNC_PRIMARY || gunfunc == FUNC_SECONDARY)) {
			return PROJECTILE_MODULE_NBOMB_STORM;
		}
		if ((weaponnum == WEAPON_GRENADE
					&& (gunfunc == FUNC_PRIMARY || gunfunc == FUNC_SECONDARY))
				|| weaponnum == WEAPON_GRENADEROUND
				|| weaponnum == WEAPON_ROCKET
				|| weaponnum == WEAPON_HOMINGROCKET
				|| weaponnum == WEAPON_SKROCKET
				|| (weaponnum == WEAPON_TIMEDMINE && gunfunc == FUNC_PRIMARY)
				|| weaponnum == WEAPON_REMOTEMINE
				|| weaponnum == WEAPON_PROXIMITYMINE
				|| (weaponnum == WEAPON_DRAGON && gunfunc == FUNC_SECONDARY)) {
			return PROJECTILE_MODULE_EXPLODE;
		}
		return 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_CONTACT:
		/* projectileTick's stick list; inside it the Devastator's wall
		 * hugger latches its wall timer. */
		if (weaponnum == WEAPON_REMOTEMINE
				|| weaponnum == WEAPON_TIMEDMINE
				|| weaponnum == WEAPON_PROXIMITYMINE
				|| weaponnum == WEAPON_COMMSRIDER
				|| weaponnum == WEAPON_TRACERBUG
				|| weaponnum == WEAPON_TARGETAMPLIFIER
				|| weaponnum == WEAPON_BOLT
				|| weaponnum == WEAPON_COMBATKNIFE
				|| weaponnum == WEAPON_ECMMINE
				|| sticks_to_wall) {
			modules |= weaponnum == WEAPON_GRENADEROUND && gunfunc == FUNC_SECONDARY
				? PROJECTILE_MODULE_WALL_HUG : PROJECTILE_MODULE_STICK;
		}
		if (weaponnum == WEAPON_BOLT || weaponnum == WEAPON_COMBATKNIFE) {
			modules |= PROJECTILE_MODULE_PIERCE;
		}
		if (weaponnum == WEAPON_ROCKET || weaponnum == WEAPON_HOMINGROCKET) {
			modules |= PROJECTILE_MODULE_IMPACT;
		}
		return modules;
	case MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED:
	case MISSION_GRAPH_EVENT_PROJECTILE_BOUNCED:
		/* A rocket that strikes anything is spent. */
		return weaponnum == WEAPON_ROCKET || weaponnum == WEAPON_HOMINGROCKET
			? PROJECTILE_MODULE_SPEND_FUSE : 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST:
		/* A grenade round goes off where it comes to rest; the N-bomb's
		 * first function too, once its fuse is armed. */
		if (weaponnum == WEAPON_GRENADEROUND) {
			return PROJECTILE_MODULE_SPEND_FUSE;
		}
		if (weaponnum == WEAPON_NBOMB && gunfunc == FUNC_PRIMARY) {
			return PROJECTILE_MODULE_SPEND_FUSE | PROJECTILE_RULE_GATE_FUSE_ARMED;
		}
		return 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_LANDED:
		if (weaponnum == WEAPON_ECMMINE
				|| weaponnum == WEAPON_COMMSRIDER
				|| weaponnum == WEAPON_TRACERBUG
				|| weaponnum == WEAPON_TARGETAMPLIFIER) {
			modules |= PROJECTILE_MODULE_SHIELD_DEVICE;
		}
		if (weaponnum == WEAPON_BOLT) {
			modules |= PROJECTILE_MODULE_LAND_BOLT;
		}
		if (weaponnum == WEAPON_COMBATKNIFE) {
			modules |= PROJECTILE_MODULE_LAND_KNIFE | PROJECTILE_MODULE_POISON_TARGET;
		}
		return modules;
	case MISSION_GRAPH_EVENT_PROJECTILE_PROXIMITY:
		/* weaponTick's proximity items, once armed. The watch list tests the
		 * Dragon at 354 units and the others at 250, and a proximity grenade
		 * ignores a moving object. */
		if (weaponnum == WEAPON_DRAGON && gunfunc == FUNC_SECONDARY) {
			return PROJECTILE_MODULE_SPEND_FUSE;
		}
		if (weaponnum == WEAPON_GRENADE && gunfunc == FUNC_SECONDARY) {
			return PROJECTILE_MODULE_SPEND_FUSE | PROJECTILE_RULE_GATE_TRIGGER_CLOSE
				| PROJECTILE_RULE_GATE_TRIGGER_CHARACTER;
		}
		if (weaponnum == WEAPON_PROXIMITYMINE
				|| (weaponnum == WEAPON_NBOMB && gunfunc == FUNC_SECONDARY)) {
			return PROJECTILE_MODULE_SPEND_FUSE | PROJECTILE_RULE_GATE_TRIGGER_CLOSE;
		}
		return 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_REMOTE_SIGNAL:
		/* Only a remote mine listens for its owner's detonator. */
		return weaponnum == WEAPON_REMOTEMINE ? PROJECTILE_MODULE_SPEND_FUSE : 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_DAMAGED:
		/* objDamage's explosives: a shot or a blast sets them off. A homing
		 * rocket ignores a blast. */
		if (weaponnum == WEAPON_HOMINGROCKET) {
			return PROJECTILE_MODULE_SPEND_FUSE | PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST;
		}
		if (weaponnum == WEAPON_GRENADE
				|| weaponnum == WEAPON_TIMEDMINE
				|| weaponnum == WEAPON_REMOTEMINE
				|| weaponnum == WEAPON_PROXIMITYMINE
				|| weaponnum == WEAPON_ROCKET
				|| weaponnum == WEAPON_GRENADEROUND
				|| (weaponnum == WEAPON_DRAGON && gunfunc == FUNC_SECONDARY)) {
			return PROJECTILE_MODULE_SPEND_FUSE;
		}
		return 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_THROWN:
		/* The thrower's weapon here: a thrown Laptop gun becomes its
		 * sentry (bgunCreateThrownProjectile2); anything else thrown is a
		 * weapon object. */
		return weaponnum == WEAPON_LAPTOPGUN ? PROJECTILE_MODULE_DEPLOY_SENTRY : 0;
	case MISSION_GRAPH_EVENT_PROJECTILE_SENTRY_AIMED:
		/* An aimed sentry fires, and so does every other autogun
		 * (autogunTick). */
		return PROJECTILE_MODULE_SENTRY_FIRE;
	default:
		return 0;
	}
}

s32 projectileRulesExplosionFromText(const char *text)
{
	s32 value = 0;
	s32 i;

	if (!text) {
		return PROJECTILE_EXPLOSION_OWN;
	}
	if (!text[0]) {
		return PROJECTILE_EXPLOSION_REFUSED;
	}
	for (i = 0; text[i]; i++) {
		if (text[i] < '0' || text[i] > '9' || i >= 3) {
			return PROJECTILE_EXPLOSION_REFUSED;
		}
		value = value * 10 + (text[i] - '0');
	}
	/* Type 0 is "no explosion": an explode that could never go off. */
	return value >= 1 && value < PROJECTILE_EXPLOSION_TYPE_COUNT
		? value : PROJECTILE_EXPLOSION_REFUSED;
}

s32 projectileRulesNativeExplosion(s32 weaponnum, s32 gunfunc)
{
	if (weaponnum == WEAPON_GRENADEROUND && gunfunc == FUNC_2) {
		return EXPLOSIONTYPE_SDGRENADE; /* the Super Dragon's grenade */
	}
	if (weaponnum == WEAPON_DRAGON) {
		return EXPLOSIONTYPE_DRAGONBOMBSPY; /* its self-destruct */
	}
	return PROJECTILE_EXPLOSION_OWN;
}

void projectileRulesLaunchedObject(s32 gunnum, s32 func, s32 thrown, s32 homing,
	s32 *weaponnum, s32 *gunfunc)
{
	s32 objweapon = gunnum;
	s32 objfunc = func;

	if (thrown) {
		/* A thrown item is itself; the thrown Laptop gun is its sentry, an
		 * autogun object (laptopDeploy). */
		if (gunnum == WEAPON_LAPTOPGUN) {
			objweapon = -1;
		}
	} else if (gunnum == WEAPON_ROCKETLAUNCHER || gunnum == WEAPON_ROCKETLAUNCHER_34
			|| gunnum == WEAPON_SLAYER) {
		/* The guards' launcher (0x2c) fires a rocket too (chrTickShoot); a
		 * player never holds it. */
		objweapon = homing ? WEAPON_HOMINGROCKET : WEAPON_ROCKET;
		objfunc = FUNC_PRIMARY;
	} else if (gunnum == WEAPON_CROSSBOW) {
		objweapon = WEAPON_BOLT;
	} else if (gunnum == WEAPON_DEVASTATOR) {
		objweapon = WEAPON_GRENADEROUND;
	} else if (gunnum == WEAPON_SUPERDRAGON) {
		objweapon = WEAPON_GRENADEROUND;
		objfunc = FUNC_2;
	}
	if (weaponnum) {
		*weaponnum = objweapon;
	}
	if (gunfunc) {
		*gunfunc = objfunc;
	}
}
