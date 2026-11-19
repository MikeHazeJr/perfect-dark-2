#ifndef _IN_PROJECTILE_EVENTS_H
#define _IN_PROJECTILE_EVENTS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Projectile events, record-only (context/designs/modding/projectile-events.md,
 * slice 1). The native projectile code calls projectileEventRecord at its
 * seams, after the native decision; nothing here changes the game. Each
 * event counts toward the stage's totals, and the first of each kind for a
 * weapon and function logs one line:
 *
 *   PROJECTILE.EVENT: <kind> weapon=<catalog id> func=<primary|secondary>
 *     object=<catalog id> projectile=<archive id or -> owner=<n> flags=0x<8>
 *
 * weapon is the gun the projectile came from (its gset), object the weapon
 * object that flies (a rocket for a rocket launcher), projectile the
 * function's projectile archive, flags the projectile's PROJECTILEFLAG_*
 * bits at that moment (B-1261). Events are the host's: a networked client
 * records none, as the object events. The totals go out with
 * projectileEventsReport, which then starts the next stage's counts.
 */

#define PROJECTILE_EVENT_SPAWNED 0    /* thrown or fired, set up, before its first tick */
#define PROJECTILE_EVENT_BOUNCED 1    /* bounced off a wall or floor */
#define PROJECTILE_EVENT_HIT_TARGET 2 /* struck a chr or a prop */
#define PROJECTILE_EVENT_LANDED 3     /* stuck or came down (objLand) */
#define PROJECTILE_EVENT_ENDED 4      /* its projectile state was freed */
#define PROJECTILE_EVENT_DETONATED 5  /* the weapon object exploded (propExplode) */
#define PROJECTILE_EVENT_COUNT 6

struct defaultobj;
struct gset;

void projectileEventRecord(s32 kind, struct defaultobj *obj);

/* Names the archive a projectile was launched from (its projectile record's
 * asset id). A fired rocket, bolt or grenade round carries its own weapon
 * number (WEAPON_ROCKET, WEAPON_BOLT, WEAPON_GRENADEROUND), not the gun's,
 * and the thrown Laptop gun is no weapon object at all, so the object alone
 * does not say which archive's program is its own. projectileApplyGraphRuntime
 * calls this at every launch that applies a record; the events and the
 * rules then name the gun and its function and use that archive's program,
 * for as long as the object lives. An object never bound is identified by
 * its own weapon and function, as everything thrown can be; a mod weapon's
 * unbound object only when its function throws the item itself (the gun on
 * a pad is not what it fires). */
void projectileEventBind(struct defaultobj *obj, const char *asset_id);

/* Logs "PROJECTILE.EVENTS: <why> spawned=N bounced=N hit_target=N landed=N
 * ended=N detonated=N fuse.compared=N fuse.differ=N rules.compared=N
 * rules.differ=N" and starts the counts again. */
void projectileEventsReport(const char *why);

/*
 * What a rule event carries for the gates that read it (projectile_rules.h):
 * the seam fills it in and passes it with the event.
 */
typedef struct projectile_rule_context {
	s32 trigger_close;     /* proximity: what came near is within 250 units */
	s32 trigger_character; /* proximity: it is a character, not a moving object */
	s32 damage_weaponnum;  /* damaged: the weapon number the damage is marked with */
} projectile_rule_context_t;

/*
 * The rule events (projectile-events.md slices 3 and 4). At a seam the
 * native code asks which modules the rule event `kind` runs for this object
 * (MISSION_GRAPH_EVENT_PROJECTILE_CONTACT, _LAUNCH_BLOCKED, _BOUNCED,
 * _CAME_TO_REST, _LANDED, _PROXIMITY, _REMOTE_SIGNAL, _DAMAGED), and then
 * runs the original code of the modules chosen (PROJECTILE_MODULE_* bits,
 * projectile_rules.h). When the object's archive has a program, the program
 * chooses: the event runs its gates and modules, and a module the event's
 * seam does not apply is dropped. With no program (a v1 archive, or an
 * object no launch bound and no archive names) the answer is the original
 * rules' choice by weapon number: `native` is projectileRulesNative's
 * answer, with the bit of each gate the original asks, and the gates are
 * answered here, the same way a program's gate nodes are. Host and client
 * alike choose by the program; only the host counts.
 *
 * The parity probe counts each choice a program made or the original would
 * have made (rules.compared) and the ones where they are not the same set
 * (rules.differ, the first eight logged as "PROJECTILE.RULE differs: ...");
 * the base programs must leave rules.differ at 0. The first choice for a
 * weapon, function and event logs "PROJECTILE.RULE: <event> weapon=...
 * func=... object=... program=... modules=a+b native=a+b".
 */
u32 projectileRuleModules(s32 kind, struct defaultobj *obj, u32 native);
u32 projectileRuleModulesWith(s32 kind, struct defaultobj *obj, u32 native,
	const projectile_rule_context_t *context);

/*
 * The sentry rules (projectile-events.md slice 5). Neither is asked about a
 * weapon object, so each is told which archive's program answers.
 *
 * projectileThrowModules: a throw is about to make its object, and
 * bgunCreateThrownProjectile2 asks what it makes
 * (event.projectile.thrown): PROJECTILE_MODULE_DEPLOY_SENTRY for a sentry
 * gun, 0 for the thrown weapon object. asset_id is the throwing function's
 * projectile archive (its launch record's), or NULL when it has none: then
 * the original answers.
 *
 * projectileSentryModules: a deployed sentry asks a rule of its own
 * (event.projectile.sentry_aimed: PROJECTILE_MODULE_SENTRY_FIRE). The
 * weapon, function and archive are the ones it was thrown from, kept with
 * the sentry since its deploy: the launch's binding does not last as long
 * as a sentry does. A mission's own turrets have no archive and never ask.
 *
 * Parity is counted with the other rules (rules.compared, rules.differ),
 * and the first decision logs the same "PROJECTILE.RULE: <event> ..." line.
 */
u32 projectileThrowModules(const struct gset *gset, const char *asset_id, u32 native);
u32 projectileSentryModules(s32 kind, struct defaultobj *obj, s32 gunnum, s32 func,
	const char *asset_id, u32 native);

/*
 * The fuse rule (projectile-events.md slices 2 and 4). Where a weapon
 * object's fuse has run out, weaponTick asks what goes off:
 * PROJECTILE_MODULE_EXPLODE, PROJECTILE_MODULE_NBOMB_STORM, both, or 0 for
 * nothing. *explosion is the explosion the explode module names
 * (og.projectile.explode's "explosion_type"), or PROJECTILE_EXPLOSION_OWN.
 * When the projectile's archive, or for a deployed item its entity's, has a
 * program (pd.projectile_graph.v2, pd.entity_graph.v2), the program decides:
 * event.projectile.timer_expired runs its modules, and a program that
 * reaches none keeps the fuse from going off. With no program (a v1 archive)
 * the original decides: `native` and `native_explosion`
 * (projectileRulesNative, projectileRulesNativeExplosion). A networked
 * client keeps the original; each peer runs its own fuses.
 *
 * The parity probe counts each expiry a program decided (fuse.compared) and
 * the ones where it did not choose what the original would have, the
 * explosion included (fuse.differ, the first eight logged as
 * "PROJECTILE.RULE differs: timer_expired ..."); the base programs must
 * leave fuse.differ at 0. The first decision for a weapon and function logs
 * "PROJECTILE.RULE: timer_expired weapon=... func=... object=... program=...
 * modules=explode native=explode explosion=own native_explosion=own".
 */
u32 projectileFuseModules(struct defaultobj *obj, u32 native, s32 native_explosion,
	s32 *explosion);

/*
 * The arm rule (projectile-events.md slice 6). Every tick weaponTick asks
 * which arm a weapon object runs: PROJECTILE_MODULE_ARM_* bits
 * (projectile_rules.h). The object chooses the first time it is asked
 * (event.projectile.armed, as it is first ticked), and the choice is kept
 * with its prop, so a tick costs one table lookup. It chooses again only
 * when its weapon number or function changes, which the original read every
 * tick (a simulant switching function rewrites the weapon it holds). When the
 * object's archive has a program the program chooses, and the answer carries
 * PROJECTILE_ARMS_PROGRAM: that object runs the arms it chose and none of a
 * mod weapon's record arms. With no program (a v1 archive, or an object no
 * archive describes) the answer is the original's arm for its weapon number
 * and function (`native`, projectileRulesNative's answer), and a mod weapon's
 * records drive their own arms as before. Host and client alike choose by
 * the program; only the host counts.
 *
 * Parity is counted with the other rules (rules.compared, rules.differ), once
 * per object, and the first decision for a weapon and function logs
 * "PROJECTILE.RULE: armed weapon=... func=... object=... program=...
 * modules=arm_fuse native=arm_fuse".
 */
#define PROJECTILE_ARMS_PROGRAM (1u << 31) /* beside the arm bits: its archive has a program */
u32 projectileArmModules(struct defaultobj *obj, u32 native);

#ifdef __cplusplus
}
#endif

#endif
