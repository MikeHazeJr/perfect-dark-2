#ifndef _IN_PROJECTILE_RULES_H
#define _IN_PROJECTILE_RULES_H

#include <PR/ultratypes.h>

/*
 * The modules a projectile program can run, the events that apply them, and
 * the original game's rules that the base programs wire
 * (context/designs/modding/projectile-events.md, slices 2 to 4). No game
 * state here, so the game, the extractor and pd-tests all read one table:
 *
 * - the extractor writes each base archive's wiring from it;
 * - the game's seams compare a program's choice with it (the parity probe)
 *   and use it for a projectile with no program (a v1 archive);
 * - registration refuses a module wired to an event that does not apply it,
 *   and a gate wired to an event that does not carry what it reads, because
 *   nothing would happen.
 *
 * A module does not run code where the program fires it. It is a choice: the
 * seam that fired the event then runs the original code for the modules
 * chosen, where that code has always run.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The modules, as bits. */
#define PROJECTILE_MODULE_EXPLODE       (1u << 0) /* og.projectile.explode: it explodes (the explosion its node names, else the weapon object's own) */
#define PROJECTILE_MODULE_STICK         (1u << 1) /* og.projectile.stick: it sticks where it struck */
#define PROJECTILE_MODULE_WALL_HUG      (1u << 2) /* og.projectile.wall_hug: it sticks, hangs for its wall timer and falls */
#define PROJECTILE_MODULE_PIERCE        (1u << 3) /* og.projectile.pierce: a bolt's or thrown knife's contact */
#define PROJECTILE_MODULE_IMPACT        (1u << 4) /* og.projectile.impact: a rocket's contact with a target */
#define PROJECTILE_MODULE_SPEND_FUSE    (1u << 5) /* og.projectile.spend_fuse: its fuse runs out now */
#define PROJECTILE_MODULE_SHIELD_DEVICE (1u << 6) /* og.projectile.shield_device: a planted device cannot be shot away */
#define PROJECTILE_MODULE_LAND_BOLT     (1u << 7) /* og.projectile.land_bolt: it lands as a crossbow bolt */
#define PROJECTILE_MODULE_LAND_KNIFE    (1u << 8) /* og.projectile.land_knife: it lands as a thrown knife */
#define PROJECTILE_MODULE_POISON_TARGET (1u << 9) /* og.projectile.poison_target: it poisons the character it sticks in */
#define PROJECTILE_MODULE_NBOMB_STORM   (1u << 10) /* og.projectile.nbomb_storm: it becomes an N-bomb storm */
#define PROJECTILE_MODULE_DEPLOY_SENTRY (1u << 11) /* og.projectile.deploy_sentry: the throw makes a sentry gun, not a weapon object */
#define PROJECTILE_MODULE_SENTRY_FIRE   (1u << 12) /* og.projectile.sentry_fire: the sentry fires */
#define PROJECTILE_MODULE_COUNT 13
#define PROJECTILE_MODULE_ALL ((1u << PROJECTILE_MODULE_COUNT) - 1)

/*
 * The gates. projectileRulesNative sets a gate's bit beside the modules the
 * original runs only when the gate passes; the wiring puts the gate in front
 * of them. A gate reads the projectile or what its event carries:
 *
 * - fuse_armed: the fuse counts or is spent (the weapon object's timer is
 *   not idle). Answers at any event.
 * - trigger_close: what came near is within 250 units. The proximity event
 *   reaches further for something the watch list tested (354 units).
 * - trigger_character: what came near is a character (a guard, a simulant,
 *   a player, an eyespy, a firing range target), not a moving object.
 * - damage_not_blast: the damage is not a blast's. The game marks an
 *   explosion's damage, a crusher's, a hoverbike's ram and a script's
 *   "destroy" with the remote mine's weapon number.
 */
#define PROJECTILE_RULE_GATE_FUSE_ARMED        (1u << 16)
#define PROJECTILE_RULE_GATE_TRIGGER_CLOSE     (1u << 17)
#define PROJECTILE_RULE_GATE_TRIGGER_CHARACTER (1u << 18)
#define PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST  (1u << 19)
#define PROJECTILE_GATE_COUNT 4
#define PROJECTILE_RULE_GATE_ALL (((1u << PROJECTILE_GATE_COUNT) - 1) << 16)
#define PROJECTILE_GATE_FUSE_ARMED_KIND "gate.projectile.fuse_armed"
#define PROJECTILE_GATE_TRIGGER_CLOSE_KIND "gate.projectile.trigger_close"
#define PROJECTILE_GATE_TRIGGER_CHARACTER_KIND "gate.projectile.trigger_character"
#define PROJECTILE_GATE_DAMAGE_NOT_BLAST_KIND "gate.projectile.damage_not_blast"

/* "og.projectile.stick" for PROJECTILE_MODULE_STICK; NULL unless the
 * argument is exactly one module bit. */
const char *projectileRulesModuleKind(u32 bit);
/* The bit of a module kind, 0 when it is not a projectile module. */
u32 projectileRulesModuleBit(const char *kind);

/* "gate.projectile.fuse_armed" for PROJECTILE_RULE_GATE_FUSE_ARMED; NULL
 * unless the argument is exactly one gate bit. */
const char *projectileRulesGateKind(u32 gate);
/* The bit of a gate kind, 0 when it is not a projectile gate. */
u32 projectileRulesGateBit(const char *kind);
/* Whether the event carries what the gate reads (1 or 0): trigger_close and
 * trigger_character only at event.projectile.proximity, damage_not_blast
 * only at event.projectile.damaged, fuse_armed at every projectile event. */
s32 projectileRulesGateApplies(u32 gate, s32 event_kind);

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
 * object with this weapon number and function, with the bit of each gate it
 * asks first. sticks_to_wall is whether that function has
 * FUNCFLAG_STICKTOWALL. 0 for an object the original never brings to the
 * event's seam (a thrown knife has no fuse; only a remote mine listens for
 * the detonator). Two events are not about a weapon object: at
 * event.projectile.thrown the weapon number and function are the thrower's
 * (a thrown Laptop gun becomes its sentry), and event.projectile.sentry_aimed
 * has one answer for every sentry and every other autogun (it fires).
 */
u32 projectileRulesNative(s32 event_kind, s32 weaponnum, s32 gunfunc, s32 sticks_to_wall);

/*
 * The explosion og.projectile.explode makes. Its node may name one with a
 * whole-number field "explosion_type" (1 to PROJECTILE_EXPLOSION_TYPE_COUNT
 * - 1, the game's explosion table); with none it is the weapon object's own:
 * the standard blast, or the huge one for an object flagged for it.
 */
#define PROJECTILE_EXPLODE_TYPE_PARAM "explosion_type"
#define PROJECTILE_EXPLOSION_OWN (-1)
#define PROJECTILE_EXPLOSION_REFUSED (-2)
#define PROJECTILE_EXPLOSION_TYPE_COUNT 26 /* EXPLOSIONTYPE_BASE_COUNT, pinned by pd-tests */
/* The field's value: PROJECTILE_EXPLOSION_OWN for NULL (no field), the
 * number for a whole number in range, PROJECTILE_EXPLOSION_REFUSED for
 * anything else. */
s32 projectileRulesExplosionFromText(const char *text);
/* The explosion the original gives a weapon object that explodes:
 * PROJECTILE_EXPLOSION_OWN, or the type it names (the Super Dragon's
 * grenade, the Dragon's self-destruct). */
s32 projectileRulesNativeExplosion(s32 weaponnum, s32 gunfunc);

/*
 * The weapon object a weapon's function launches: its weapon number and
 * function. A thrown item is itself. A fired rocket, bolt or grenade round
 * is its own weapon (bgunCreateFiredProjectile for a player, chrTickShoot
 * for a character): the rocket launcher, the guards' launcher
 * (WEAPON_ROCKETLAUNCHER_34) and the Slayer fire WEAPON_ROCKET, or
 * WEAPON_HOMINGROCKET from a homing function; the crossbow WEAPON_BOLT; the
 * Devastator WEAPON_GRENADEROUND; the Super Dragon WEAPON_GRENADEROUND with
 * function 2. *weaponnum is -1
 * when what is launched is no weapon object (the thrown Laptop gun is an
 * autogun).
 */
void projectileRulesLaunchedObject(s32 gunnum, s32 func, s32 thrown, s32 homing,
	s32 *weaponnum, s32 *gunfunc);

#ifdef __cplusplus
}
#endif

#endif
