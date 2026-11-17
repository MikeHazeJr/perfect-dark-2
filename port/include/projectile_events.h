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

/* Logs "PROJECTILE.EVENTS: <why> spawned=N bounced=N hit_target=N landed=N
 * ended=N detonated=N" and starts the counts again. */
void projectileEventsReport(const char *why);

#ifdef __cplusplus
}
#endif

#endif
