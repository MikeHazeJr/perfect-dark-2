#ifndef IN_GAME_SPAWNPOOL_H
#define IN_GAME_SPAWNPOOL_H

#include <ultra64.h>
#include "data.h"
#include "types.h"
#include "constants.h"

/*
 * spawnpool.h -- Universal spawn pool system (L1-L4 fallback hierarchy)
 *
 * Guarantees that N players + M bots can always spawn on any map, even with
 * zero declared spawn points. Deterministic: same seed = same pool on all
 * clients.
 *
 * Usage:
 *   After playerReset() populates g_SpawnPoints[] from INTROCMD_SPAWN,
 *   call spawnPoolBuild() to construct the validated pool. The pool replaces
 *   raw g_SpawnPoints[] as the source for playerChooseSpawnLocation().
 *
 * Design ref: context/designs/spawn-system-architecture-2026-04-13.md
 */

#define SPAWNPOOL_MAX       MAX_MPCHRS  /* 40 (8 players + 32 bots) */
#define SPAWNPOOL_RAY_COUNT 18          /* 6 cardinal + 4 XZ diag + 4 upper + 4 lower */
#define SPAWNPOOL_RAY_RANGE 2000.0f     /* max ray distance (units) */
/* Budget threshold unchanged (1500) even though ray count grew to 18 — the
 * extra rays are safety nets (lower diagonals catch overhangs below), not
 * harder gates. Keeping the threshold avoids regressing stages that used to
 * produce valid pools. */
#define SPAWNPOOL_BUDGET_THRESHOLD 1500.0f /* min sum of ray distances */
#define SPAWNPOOL_L4_MAX_DILATIONS 8
/* Player capsule radius (units). Any ray hit closer than this means the
 * capsule would intersect the surface -- reject the candidate immediately. */
#define SPAWNPOOL_CAPSULE_RADIUS 30.0f

/* Which layer generated this spawn point */
#define SPAWNLAYER_DECLARED  1  /* L1: INTROCMD_SPAWN pad */
#define SPAWNLAYER_WAYPOINT  2  /* L2: navmesh/waypoint sampling */
#define SPAWNLAYER_GRID      3  /* L3: AABB grid raycast */
#define SPAWNLAYER_RADIAL    4  /* L4: centroid + radial offsets */

/*
 * S302: runtime selection tiers.
 *
 * Distinct from the BUILD layers above: these describe how we PICKED a
 * point from an already-built pool.  Each spawn decision logs the tier
 * that produced it so playtest traces show the tier distribution
 * (healthy = mostly T1; heavy T3/T4 = pool is being exhausted and the
 * map / participant count need attention).
 *
 *   T1 OPTIMAL      — farthest-point-first over unused + unreserved
 *                     slots.  Ideal case; the pool had spare capacity.
 *   T2 CYCLED       — same-tick reservation bitset was exhausted; we
 *                     cleared the reservations and retried (unused slots
 *                     from prior ticks are back in play).  Still ideal
 *                     quality, just indicates burst pressure.
 *   T3 REUSED       — every slot is either reserved OR occupied by a
 *                     live player; we picked the slot farthest from all
 *                     occupied positions.  The spawn may briefly overlap
 *                     another player, but telefragging is the designed
 *                     behaviour for over-subscribed maps.
 *   T4 LAST_RESORT  — pool is effectively unusable (empty or every slot
 *                     failed validation).  Caller falls back to legacy
 *                     pad selection or a radial offset from center.
 */
typedef enum spawn_select_tier {
	SPAWN_TIER_NONE        = 0,
	SPAWN_TIER_1_OPTIMAL   = 1,
	SPAWN_TIER_2_CYCLED    = 2,
	SPAWN_TIER_3_REUSED    = 3,
	SPAWN_TIER_4_LASTRESORT = 4,
} spawn_select_tier_t;

/* Human-readable name for SPAWN.TIER log lines. */
const char *spawnPoolTierName(spawn_select_tier_t tier);

typedef struct spawn_point {
	struct coord pos;
	RoomNum room;
	s16 source_pad;    /* pad number if from L1/L2, -1 if synthetic */
	u8 layer;          /* SPAWNLAYER_* */
	f32 budget_score;  /* raycast budget score (sum of ray distances) */
	f32 angle_rad;     /* S298: facing-angle from 8-direction wall probe
	                    *       (radians, 0 = +Z).  Faces away from the average
	                    *       wall normal so the chr doesn't spawn staring
	                    *       straight at a wall. */
} spawn_point_t;

typedef struct spawn_pool {
	spawn_point_t points[SPAWNPOOL_MAX];
	s32 count;
	u32 seed;
	s32 needed;
	u8 max_layer_used; /* highest layer that contributed points */
} spawn_pool_t;

/* Axis-aligned bounding box for stage geometry */
typedef struct spawn_aabb {
	struct coord min;
	struct coord max;
	bool valid;
} spawn_aabb_t;

/*
 * Build the validated spawn pool. Call after playerReset() has run the
 * INTROCMD_SPAWN pass (g_SpawnPoints/g_NumSpawnPoints are populated).
 *
 * stage_id:   catalog ID string for deterministic hashing
 * match_seed: server-generated seed distributed via SVC_STAGE_START
 * needed:     number of spawn points required (players + bots)
 *
 * The builder attempts to satisfy needed points, but may return fewer when
 * geometry constraints make additional valid points impossible.
 */
void spawnPoolBuild(spawn_pool_t *pool, const char *stage_id,
                    u32 match_seed, s32 needed);

/*
 * Compute axis-aligned bounding box of the loaded stage geometry.
 * Uses g_Rooms[].bbmin/bbmax. Returns .valid = false if no rooms loaded.
 */
void spawnPoolComputeAABB(spawn_aabb_t *aabb);

/*
 * Raycast-budget validation for a single candidate point.
 * Fires SPAWNPOOL_RAY_COUNT rays outward, checks for backface hits,
 * sums ray distances. Returns budget score (>= 0). Returns -1.0f if
 * candidate is inside geometry (backface hit detected).
 *
 * The candidate passes if score >= SPAWNPOOL_BUDGET_THRESHOLD.
 */
f32 spawnPoolRaycastBudget(const struct coord *pos, RoomNum room);

/*
 * Full validation of a candidate spawn point: ground clearance, vertical
 * clearance, room validity, not-inside-geometry, minimum spacing, and
 * raycast-budget. Returns budget score if valid, -1.0f if rejected.
 */
f32 spawnPoolValidateCandidate(const struct coord *pos, RoomNum room,
                               const spawn_point_t *existing, s32 num_existing,
                               f32 min_spacing);

/* Get the global spawn pool (built by spawnPoolBuild, lives until next build) */
const spawn_pool_t *spawnPoolGet(void);

/* Check if the pool has been built for the current stage */
bool spawnPoolIsReady(void);

/* Build the global pool instance (called from playerreset.c after INTROCMD pass) */
void spawnPoolBuildGlobal(const char *stage_id, u32 match_seed, s32 needed);

/* Reset pool state on stage change */
void spawnPoolReset(void);

/*
 * Select a spawn point using farthest-point-first greedy algorithm.
 * Maximizes the minimum distance from already-occupied positions.
 *
 * occupied:      array of positions already assigned to other participants
 * num_occupied:  number of entries in occupied[]
 * team:          team index (0-3) for team-aware selection, or -1 for FFA
 * num_teams:     total number of teams in this match (0 = FFA)
 * pool_center:   map center (for team sector computation)
 *
 * Returns index into pool->points[], or -1 if pool is empty.
 */
s32 spawnPoolSelect(const spawn_pool_t *pool, const struct coord *occupied,
                    s32 num_occupied, s32 team, s32 num_teams,
                    const struct coord *pool_center);

/*
 * S302: tiered select — same contract as spawnPoolSelect but cascades
 * through T1 -> T2 -> T3 before returning -1 (T4 is the caller's
 * responsibility).  out_tier receives the tier that produced the
 * returned index (SPAWN_TIER_NONE on -1).
 *
 * teammate_positions / num_teammates: when teams are enabled and
 * num_teammates > 0, each candidate is scored with min-distance-to-
 * occupied plus a weighted mean (1/N) of XZ dot products from pool_center
 * toward each teammate — preferring spawns on the same side of the map
 * as the group.  ~20% of calls ignore this term (relax) to reduce spawn
 * oscillation.  Pass NULL / 0 when unused.
 *
 * Guarantees at least one valid selection when pool->count > 0, unless
 * every slot has `used` flagged by occupied[] AND the pool has fewer
 * slots than occupied (physically impossible to avoid overlap).  Even
 * then T3 returns the farthest-from-occupied slot so the caller never
 * has to pick blindly.
 */
s32 spawnPoolSelectTiered(const spawn_pool_t *pool,
                          const struct coord *occupied, s32 num_occupied,
                          s32 team, s32 num_teams,
                          const struct coord *pool_center,
                          const struct coord *teammate_positions, s32 num_teammates,
                          spawn_select_tier_t *out_tier);

/*
 * S302: last-resort position when the pool is unavailable or empty.
 * Picks the point farthest from `occupied` — uses a pool slot if any
 * exist (even ones with failed validation), else synthesises a radial
 * position around the AABB centre.  Always writes a non-void pos +
 * room.  Returns the tier that produced the answer (T3 if a pool slot
 * was reused, T4 if synthesised).
 *
 * Exists so every caller can reach a final position without
 * re-implementing the fallback cascade locally.
 */
spawn_select_tier_t spawnPoolLastResort(const struct coord *occupied,
                                        s32 num_occupied,
                                        struct coord *out_pos,
                                        RoomNum *out_room,
                                        f32 *out_angle);

/*
 * S298: same-tick reservation bitset.
 *
 * Adjacent spawnPoolSelect() calls in the same tick (e.g. placing every
 * bot at match start) only know about slot positions in `occupied[]`.  If
 * the caller hasn't finished writing a peer's pos yet, the peer doesn't
 * appear in occupied[], and two calls can pick the same pool index.
 *
 * Reservations fix that: spawnPoolSelect() internally skips reserved
 * indices and marks its chosen slot reserved.  Callers orchestrating a
 * burst of selections should call spawnPoolClearReservations() at the
 * start of the burst (or per-tick).  Reservations are also auto-cleared
 * whenever the pool is rebuilt (spawnPoolBuild / spawnPoolReset).
 */
void spawnPoolClearReservations(void);

/*
 * Log current pool state; summary of accumulated session data.
 * Returns count of stages that needed L3 or L4.
 */
s32 spawnPoolSmokeTest(void);

/*
 * Iterate all catalog arenas and build offline pools (zero declared pads).
 * Live (in-game) results take priority over offline results.
 * Returns count of arenas that needed L3 or L4.
 */
s32 spawnPoolSmokeAll(void);

/*
 * Write smoke log CSV to path.
 * Columns: stage_id, needed, produced, max_layer_used, source, time_ms, flag
 * source: 'L' = live gameplay, 'O' = offline sweep
 * flag: OK or L3L4_RISK
 */
void spawnPoolSmokeWriteCSV(const char *path);

/*
 * Append forge-authored spawn points to the already-built global pool.
 * Called from forge_runtime.c on FREEFLY->NORMAL transition after
 * spawnPoolBuildGlobal() has already run at stage load.
 *
 * positions:    world-space positions (count entries)
 * facing_rads:  author-set facing angles in radians (count entries, or NULL)
 * count:        number of points to inject
 *
 * Returns the number of points actually appended (capped at SPAWNPOOL_MAX).
 */
s32 spawnPoolAppendForgePoints(const struct coord *positions,
                               const f32 *facing_rads,
                               s32 count);

#endif /* IN_GAME_SPAWNPOOL_H */
