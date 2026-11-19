/*
 * projectile_rules.c -- the modules of a projectile program and the original
 * game's rules by weapon number: see projectile_rules.h and
 * context/designs/modding/projectile-events.md (slice 3).
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
	{ PROJECTILE_MODULE_FUSE_EXPIRED, "og.projectile.fuse_expired" },
	{ PROJECTILE_MODULE_STICK, "og.projectile.stick" },
	{ PROJECTILE_MODULE_WALL_HUG, "og.projectile.wall_hug" },
	{ PROJECTILE_MODULE_PIERCE, "og.projectile.pierce" },
	{ PROJECTILE_MODULE_IMPACT, "og.projectile.impact" },
	{ PROJECTILE_MODULE_SPEND_FUSE, "og.projectile.spend_fuse" },
	{ PROJECTILE_MODULE_SHIELD_DEVICE, "og.projectile.shield_device" },
	{ PROJECTILE_MODULE_LAND_BOLT, "og.projectile.land_bolt" },
	{ PROJECTILE_MODULE_LAND_KNIFE, "og.projectile.land_knife" },
	{ PROJECTILE_MODULE_POISON_TARGET, "og.projectile.poison_target" },
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
		PROJECTILE_MODULE_FUSE_EXPIRED },
	{ MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, "event.projectile.contact",
		PROJECTILE_MODULE_STICK | PROJECTILE_MODULE_WALL_HUG | PROJECTILE_MODULE_PIERCE
			| PROJECTILE_MODULE_IMPACT },
	{ MISSION_GRAPH_EVENT_PROJECTILE_LAUNCH_BLOCKED, "event.projectile.launch_blocked",
		PROJECTILE_MODULE_SPEND_FUSE },
	{ MISSION_GRAPH_EVENT_PROJECTILE_CAME_TO_REST, "event.projectile.came_to_rest",
		PROJECTILE_MODULE_SPEND_FUSE },
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
		/* Every weapon's fuse is its own (weaponTick's four fuses). */
		return PROJECTILE_MODULE_FUSE_EXPIRED;
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
	default:
		return 0;
	}
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
	} else if (gunnum == WEAPON_ROCKETLAUNCHER || gunnum == WEAPON_SLAYER) {
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
