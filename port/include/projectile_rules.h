#ifndef _IN_PROJECTILE_RULES_H
#define _IN_PROJECTILE_RULES_H

#include <PR/ultratypes.h>

/*
 * The modules a projectile program can run, the events that apply them, and
 * the original game's rules that the base programs wire
 * (context/designs/modding/projectile-events.md, slices 2 to 6). No game
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
/*
 * Slice 6: the arms. A weapon object chooses its arm once, when it is first
 * ticked (event.projectile.armed), and weaponTick runs that arm from then
 * on: what the object's timer means and what it listens for. "The fuse rule"
 * below is event.projectile.timer_expired, which says what goes off. The
 * original gives an object one arm. A program may choose several, and then
 * the first chosen in this order runs: arm_fuse, arm_wall_fuse and
 * arm_mine_fuse step aside while the timer is idle, the others never do.
 *
 * - arm_fuse: while its timer is not idle it counts down; below zero the
 *   fuse rule is asked, and if it chooses anything the object is removed.
 * - arm_wall_fuse: arm_fuse for a wall hugger. While its timer is above zero
 *   it is the time the object flies or hangs before it lets go and drops;
 *   the landing zeroes it (og.projectile.wall_hug), and from zero it is
 *   arm_fuse's.
 * - arm_struck_fuse: no countdown. A spent fuse (a timer of zero) asks the
 *   fuse rule, and if it chooses anything the object is removed.
 * - arm_mine_fuse: while its timer is not idle it counts down; below zero
 *   the fuse rule is asked, and the object is removed only if something
 *   went off (an explosion can fail for want of room: the mine then stays).
 * - arm_remote: it listens for its owner's detonator
 *   (event.projectile.remote_signal). A spent fuse asks the fuse rule until
 *   something goes off.
 * - arm_proximity: its timer is its arming delay; armed, it joins the watch
 *   list and tests the player of the tick (event.projectile.proximity). A
 *   spent fuse asks the fuse rule until something goes off.
 * - arm_bolt: a crossbow bolt's quiver after it lands, and its beam.
 */
#define PROJECTILE_MODULE_ARM_FUSE        (1u << 13) /* og.projectile.arm_fuse */
#define PROJECTILE_MODULE_ARM_WALL_FUSE   (1u << 14) /* og.projectile.arm_wall_fuse */
#define PROJECTILE_MODULE_ARM_STRUCK_FUSE (1u << 15) /* og.projectile.arm_struck_fuse */
#define PROJECTILE_MODULE_ARM_MINE_FUSE   (1u << 16) /* og.projectile.arm_mine_fuse */
#define PROJECTILE_MODULE_ARM_REMOTE      (1u << 17) /* og.projectile.arm_remote */
#define PROJECTILE_MODULE_ARM_PROXIMITY   (1u << 18) /* og.projectile.arm_proximity */
#define PROJECTILE_MODULE_ARM_BOLT        (1u << 19) /* og.projectile.arm_bolt */
/*
 * projectile-events.md slice 7: the flight rule. A weapon object chooses how
 * it flies, and how it looks and sounds in flight, the first tick it flies
 * (event.projectile.in_flight); projectileTick runs the modules it chose
 * where it tested weapon numbers. Their settings (node members) carry what
 * the original keyed by weapon number: projectile_flight_t below.
 */
#define PROJECTILE_MODULE_FLY_BY_WIRE  (1u << 20) /* og.projectile.fly_by_wire: rocketTickFbw flies it (the Skedar rocket) */
#define PROJECTILE_MODULE_THRUST       (1u << 21) /* og.projectile.thrust: a speed cap, the power limit and an exhaust (the rocket) */
#define PROJECTILE_MODULE_HOME         (1u << 22) /* og.projectile.home: it steers at its target (the homing rocket) */
#define PROJECTILE_MODULE_TRAIL        (1u << 23) /* og.projectile.trail: a smoke a tick (the homing rocket, the grenade round) */
#define PROJECTILE_MODULE_WOOSH        (1u << 24) /* og.projectile.woosh: the thrown knife's woosh */
#define PROJECTILE_MODULE_HIT_SOUND    (1u << 25) /* og.projectile.hit_sound: its collision sound (the knife, the pinball) */
#define PROJECTILE_MODULE_BOUNCE_SMOKE (1u << 26) /* og.projectile.bounce_smoke: a puff where it bounces (the pinball) */
#define PROJECTILE_MODULE_COUNT 27
#define PROJECTILE_MODULE_FLIGHT \
	(PROJECTILE_MODULE_FLY_BY_WIRE | PROJECTILE_MODULE_THRUST | PROJECTILE_MODULE_HOME | PROJECTILE_MODULE_TRAIL \
		| PROJECTILE_MODULE_WOOSH | PROJECTILE_MODULE_HIT_SOUND | PROJECTILE_MODULE_BOUNCE_SMOKE)
#define PROJECTILE_MODULE_ALL ((1u << PROJECTILE_MODULE_COUNT) - 1)
#define PROJECTILE_MODULE_ARMS \
	(PROJECTILE_MODULE_ARM_FUSE | PROJECTILE_MODULE_ARM_WALL_FUSE | PROJECTILE_MODULE_ARM_STRUCK_FUSE \
		| PROJECTILE_MODULE_ARM_MINE_FUSE | PROJECTILE_MODULE_ARM_REMOTE | PROJECTILE_MODULE_ARM_PROXIMITY \
		| PROJECTILE_MODULE_ARM_BOLT)

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
/* Slice 7 moved them from bits 24 to 27 up to 27 to 30, above the flight
 * modules and below the program flag of an arm's or a flight's answer (31).
 * Archives name kinds, never bits. */
#define PROJECTILE_RULE_GATE_FUSE_ARMED        (1u << 27)
#define PROJECTILE_RULE_GATE_TRIGGER_CLOSE     (1u << 28)
#define PROJECTILE_RULE_GATE_TRIGGER_CHARACTER (1u << 29)
#define PROJECTILE_RULE_GATE_DAMAGE_NOT_BLAST  (1u << 30)
#define PROJECTILE_GATE_COUNT 4
#define PROJECTILE_RULE_GATE_ALL (((1u << PROJECTILE_GATE_COUNT) - 1) << 27)
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
 * has one answer for every sentry and every other autogun (it fires). At
 * event.projectile.armed the answer is the arm weaponTick gives that weapon
 * number and function: one arm, or none for an object with no timer to run
 * (a gun, a knife, a planted device).
 */
u32 projectileRulesNative(s32 event_kind, s32 weaponnum, s32 gunfunc, s32 sticks_to_wall);

/*
 * projectile-events.md slice 7: the flight modules' settings.
 *
 * - og.projectile.thrust: "max_speed" (units a tick, above 0 and up to
 *   10000; the rocket's 166.666656, whose square is the original's
 *   27777.773) and "exhaust" (a smoke, or none; the rocket's "rocket"), the
 *   smoke it leaves 20 units behind while powered.
 * - og.projectile.trail: "smoke" (the homing rocket's "homing", the grenade
 *   round's "grenade") and "while" ("flying": not colliding, the homing
 *   rocket's; "moving": not at rest, the grenade round's).
 * - og.projectile.hit_sound: "sound" ("knife", "pinball").
 * - og.projectile.bounce_smoke: "smoke" (the pinball's "pinball").
 *
 * The smokes: "rocket", "homing", "grenade" (the record's trail_type names)
 * and "pinball". A setting left out is the original's for the object's
 * weapon number, else the defaults below.
 */
#define PROJECTILE_THRUST_MAX_SPEED_PARAM "max_speed"
#define PROJECTILE_THRUST_EXHAUST_PARAM "exhaust"
#define PROJECTILE_SMOKE_PARAM "smoke"
#define PROJECTILE_TRAIL_WHILE_PARAM "while"
#define PROJECTILE_HIT_SOUND_PARAM "sound"
#define PROJECTILE_TRAIL_WHILE_FLYING 1 /* not colliding */
#define PROJECTILE_TRAIL_WHILE_MOVING 2 /* not at rest */
#define PROJECTILE_HIT_SOUND_KNIFE 1
#define PROJECTILE_HIT_SOUND_PINBALL 2
#define PROJECTILE_ROCKET_MAX_SPEED 166.666656f /* squared, the original's 27777.773 */
#define PROJECTILE_MAX_SPEED_LIMIT 10000

typedef struct projectile_flight {
	f32 max_speed;    /* thrust: its cap, units a tick */
	s16 exhaust;      /* thrust: the smoke it leaves while powered, -1 none */
	s16 trail_smoke;  /* trail: its smoke */
	s8 trail_while;   /* trail: PROJECTILE_TRAIL_WHILE_* */
	s8 hit_sound;     /* hit_sound: PROJECTILE_HIT_SOUND_* */
	s16 bounce_smoke; /* bounce_smoke: its smoke */
} projectile_flight_t;

/* The original's settings for a weapon object's flight modules (the rocket's
 * cap and exhaust, the homing rocket's or the grenade round's trail, the
 * knife's or the pinball's collision sound and the pinball's puff); for an
 * object the original gives none of a module, that module's defaults (the
 * rocket's thrust, the homing trail, the knife's sound, the pinball's puff). */
void projectileRulesNativeFlight(s32 weaponnum, s32 gunfunc, projectile_flight_t *out);
/* A module's settings, read by `get` (a node's text for a key, NULL when it
 * has none), over the defaults in *flight. Returns 1, or 0 when a setting is
 * malformed (it keeps its default, and *bad_key, when given, names it). */
typedef const char *(*projectile_text_fn)(const char *key, void *user);
s32 projectileRulesFlightSettings(u32 module, projectile_text_fn get, void *user, projectile_flight_t *flight,
	const char **bad_key);
/* What a setting takes, for refusals ("rocket, homing, grenade or pinball"). */
const char *projectileRulesFlightSettingHelp(const char *key);
/* The settings a module's node carries, as JSON members (", \"smoke\":
 * \"homing\", ..."), for the extractor: the length written, -1 when it does
 * not fit. A module with no settings writes "". */
s32 projectileRulesFlightSettingsText(u32 module, const projectile_flight_t *flight, char *buf, s32 cap);
/* Whether two flights' settings agree for the modules given. */
s32 projectileRulesFlightSame(u32 modules, const projectile_flight_t *a, const projectile_flight_t *b);
/* A smoke's name ("rocket"), NULL for none of these. */
const char *projectileRulesSmokeName(s32 smoketype);

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
