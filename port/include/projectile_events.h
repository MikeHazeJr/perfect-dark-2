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

void projectileEventRecord(s32 kind, struct defaultobj *obj);

/* Names the archive a projectile was launched from (its projectile record's
 * asset id). A fired rocket, bolt or grenade round carries its own weapon
 * number (WEAPON_ROCKET, WEAPON_BOLT, WEAPON_GRENADEROUND), not the gun's,
 * and the thrown Laptop gun is no weapon object at all, so the object alone
 * does not say which archive's program is its own. projectileApplyGraphRuntime
 * calls this at every launch that applies a record; the events and the fuse
 * rule then name the gun and its function and use that archive's program. An
 * object never bound is identified by its own weapon and function, as
 * everything thrown can be. */
void projectileEventBind(struct defaultobj *obj, const char *asset_id);

/* Logs "PROJECTILE.EVENTS: <why> spawned=N bounced=N hit_target=N landed=N
 * ended=N detonated=N fuse.compared=N fuse.differ=N" and starts the counts
 * again. */
void projectileEventsReport(const char *why);

/*
 * The fuse rule (projectile-events.md slice 2). At a fuse expiry the native
 * code in weaponTick asks whether to run its original fuse (the explosion or
 * the N-bomb storm). When the projectile's archive, or for a deployed item
 * its entity's, has a program (pd.projectile_graph.v2, pd.entity_graph.v2),
 * the program decides: event.projectile.timer_expired runs
 * og.projectile.fuse_expired, and a program that does not reach it keeps the
 * fuse from running (fail closed). With no program (a v1 archive) the native
 * rule decides, as before. A networked client keeps the native rule; the
 * host's outcome replicates.
 *
 * The parity probe counts each expiry a program decided (fuse.compared) and
 * the ones where it did not run the fuse the original would have run
 * (fuse.differ); the base programs must leave fuse.differ at 0. The first
 * decision for a weapon and function logs "PROJECTILE.RULE: timer_expired
 * weapon=... func=... object=... program=... fuse=0|1".
 */
s32 projectileFuseRule(struct defaultobj *obj);

#ifdef __cplusplus
}
#endif

#endif
