#ifndef _IN_PROJECTILE_RULES_H
#define _IN_PROJECTILE_RULES_H

#include <PR/ultratypes.h>

/*
 * The modules a projectile program can run, the events that apply them, and
 * the original game's rules that the base programs wire
 * (context/designs/modding/projectile-events.md, slices 2 and 3). No game
 * state here, so the game, the extractor and pd-tests all read one table:
 *
 * - the extractor writes each base archive's wiring from it;
 * - the game's seams compare a program's choice with it (the parity probe)
 *   and use it for a projectile with no program (a v1 archive);
 * - registration refuses a module wired to an event that does not apply it,
 *   because nothing would happen.
 *
 * A module does not run code where the program fires it. It is a choice: the
 * seam that fired the event then runs the original code for the modules
 * chosen, where that code has always run.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The modules, as bits. */
#define PROJECTILE_MODULE_FUSE_EXPIRED  (1u << 0) /* og.projectile.fuse_expired: the weapon's original fuse (explosion or N-bomb storm) */
#define PROJECTILE_MODULE_STICK         (1u << 1) /* og.projectile.stick: it sticks where it struck */
#define PROJECTILE_MODULE_WALL_HUG      (1u << 2) /* og.projectile.wall_hug: it sticks, hangs for its wall timer and falls */
#define PROJECTILE_MODULE_PIERCE        (1u << 3) /* og.projectile.pierce: a bolt's or thrown knife's contact */
#define PROJECTILE_MODULE_IMPACT        (1u << 4) /* og.projectile.impact: a rocket's contact with a target */
#define PROJECTILE_MODULE_SPEND_FUSE    (1u << 5) /* og.projectile.spend_fuse: its fuse runs out now */
#define PROJECTILE_MODULE_SHIELD_DEVICE (1u << 6) /* og.projectile.shield_device: a planted device cannot be shot away */
#define PROJECTILE_MODULE_LAND_BOLT     (1u << 7) /* og.projectile.land_bolt: it lands as a crossbow bolt */
#define PROJECTILE_MODULE_LAND_KNIFE    (1u << 8) /* og.projectile.land_knife: it lands as a thrown knife */
#define PROJECTILE_MODULE_POISON_TARGET (1u << 9) /* og.projectile.poison_target: it poisons the character it sticks in */
#define PROJECTILE_MODULE_COUNT 10
#define PROJECTILE_MODULE_ALL ((1u << PROJECTILE_MODULE_COUNT) - 1)

/* projectileRulesNative sets this beside a module the original runs only
 * while the fuse is armed (the weapon object's timer is not idle); the
 * wiring puts gate.projectile.fuse_armed in front of it. */
#define PROJECTILE_RULE_GATE_FUSE_ARMED (1u << 16)
#define PROJECTILE_GATE_FUSE_ARMED_KIND "gate.projectile.fuse_armed"

/* "og.projectile.stick" for PROJECTILE_MODULE_STICK; NULL unless the
 * argument is exactly one module bit. */
const char *projectileRulesModuleKind(u32 bit);
/* The bit of a module kind, 0 when it is not a projectile module. */
u32 projectileRulesModuleBit(const char *kind);

/* The projectile events, in kind order: the index-th event's kind, or -1
 * past the end. */
s32 projectileRulesEventAt(s32 index);
/* "event.projectile.contact" for its kind; NULL for no projectile event. */
const char *projectileRulesEventName(s32 event_kind);
/* The modules the event's seam applies. 0 for an event that applies none
 * (spawned, hit_target, ended, detonated are records). */
u32 projectileRulesEventModules(s32 event_kind);

/*
 * The original game's rule: the modules it runs at the event for a weapon
 * object with this weapon number and function. sticks_to_wall is whether
 * that function has FUNCFLAG_STICKTOWALL. PROJECTILE_RULE_GATE_FUSE_ARMED
 * may be set beside PROJECTILE_MODULE_SPEND_FUSE (the N-bomb at rest).
 */
u32 projectileRulesNative(s32 event_kind, s32 weaponnum, s32 gunfunc, s32 sticks_to_wall);

/*
 * The weapon object a weapon's function launches: its weapon number and
 * function. A thrown item is itself. A fired rocket, bolt or grenade round
 * is its own weapon (bgunCreateFiredProjectile): the rocket launcher and
 * the Slayer fire WEAPON_ROCKET, or WEAPON_HOMINGROCKET from a homing
 * function; the crossbow WEAPON_BOLT; the Devastator WEAPON_GRENADEROUND;
 * the Super Dragon WEAPON_GRENADEROUND with function 2. *weaponnum is -1
 * when what is launched is no weapon object (the thrown Laptop gun is an
 * autogun).
 */
void projectileRulesLaunchedObject(s32 gunnum, s32 func, s32 thrown, s32 homing,
	s32 *weaponnum, s32 *gunfunc);

#ifdef __cplusplus
}
#endif

#endif
