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
#define SPAWNPOOL_RAY_COUNT 14          /* 6 cardinal + 8 horizontal diag */
#define SPAWNPOOL_RAY_RANGE 2000.0f     /* max ray distance (units) */
#define SPAWNPOOL_BUDGET_THRESHOLD 1500.0f /* min sum of ray distances */
#define SPAWNPOOL_L4_MAX_DILATIONS 8

/* Which layer generated this spawn point */
#define SPAWNLAYER_DECLARED  1  /* L1: INTROCMD_SPAWN pad */
#define SPAWNLAYER_WAYPOINT  2  /* L2: navmesh/waypoint sampling */
#define SPAWNLAYER_GRID      3  /* L3: AABB grid raycast */
#define SPAWNLAYER_RADIAL    4  /* L4: centroid + radial offsets */

typedef struct spawn_point {
	struct coord pos;
	RoomNum room;
	s16 source_pad;    /* pad number if from L1/L2, -1 if synthetic */
	u8 layer;          /* SPAWNLAYER_* */
	f32 budget_score;  /* raycast budget score (sum of ray distances) */
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
 * After return, pool->count >= needed is guaranteed.
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

#endif /* IN_GAME_SPAWNPOOL_H */
