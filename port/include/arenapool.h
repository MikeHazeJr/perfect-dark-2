#ifndef _IN_PORT_ARENAPOOL_H
#define _IN_PORT_ARENAPOOL_H

#include <stddef.h>
#include "PR/ultratypes.h"
#include "memarena.h"

/*
 * arenapool -- a growable, pointer-stable slot pool on top of memarena (task #39).
 *
 * Wraps the common "fixed-size stage pool" pattern that recurs across the game:
 *   g_MaxX = N;
 *   g_X = mempAlloc(g_MaxX * sizeof(struct X), MEMPOOL_STAGE);
 *   for (i = 0; i < g_MaxX; i++) g_X[i].<free-marker> = ...;
 * (projectiles, embedments, debris, weapon/hat/ammo slots, explosions, ...). Each
 * had a hard cap that either silently dropped or evicted a live entry when full.
 *
 * An arenapool reserves a large VIRTUAL range once (zero RAM) and commits pages on
 * demand, so the base never moves -- interior pointers held into the pool
 * (obj->projectile, prop->obj, ...) stay valid across growth. The pool's logical
 * size is tracked in `count`; the owning code keeps its g_MaxX mirror in sync from
 * arenaPoolSetup/Grow return values. Growth is bounded only by the (generous,
 * loud-failing) reservation. See memarena.h for why contiguous reserve-commit,
 * not a linked block-list, is the right shape here.
 *
 * Usage per stage (in the pool's reset path):
 *   g_X = arenaPoolSetup(&s_XPool, "x", sizeof(struct X), RESERVE_MAX, CHUNK, g_MaxX);
 *   for (i = 0; i < g_MaxX; i++) g_X[i].<free-marker> = ...;
 * On overflow (in the allocator, when no free slot is found):
 *   s32 first; g_X = arenaPoolGrow(&s_XPool, &first);
 *   if (first >= 0) { g_MaxX = s_XPool.count; init g_X[first..g_MaxX); use g_X[first]; }
 */
struct arenapool {
	struct memarena arena;
	s32 count;  /* logical committed slot count (mirror of the owner's g_MaxX) */
	s32 chunk;  /* grow granularity                                            */
};

/*
 * Reserve + (re)commit to `count` slots for a fresh stage. Reserves the virtual
 * range on first use (idempotent across stages) using stride/reservemax/chunk,
 * then commits enough pages for `count` slots and records p->count = count.
 * Returns the stable base pointer, or NULL on failure (caller should treat like
 * the old mempAlloc-returned-NULL and leave its g_X pointer NULL). Slots are NOT
 * zeroed here -- the caller's existing init loop sets the free markers.
 */
void *arenaPoolSetup(struct arenapool *p, const char *name, size_t stride,
		size_t reservemax, s32 chunk, s32 count);

/*
 * Grow the pool by one chunk. Commits more pages (base stays put), sets *firstnew
 * to the first newly-available slot index, advances p->count, and returns the
 * stable base. On exhaustion (reservation full or not yet set up) returns NULL and
 * sets *firstnew to -1, leaving p->count unchanged. The caller inits
 * [*firstnew, p->count) and updates its g_MaxX to p->count.
 */
void *arenaPoolGrow(struct arenapool *p, s32 *firstnew);

/* Decommit all pages but keep the reservation (optional; setup reuses pages if
 * this is not called). */
void arenaPoolReset(struct arenapool *p);

#endif
