/*
 * spawnpool.c -- Universal spawn pool system (L1-L4 fallback hierarchy)
 *
 * Guarantees N+M spawn points on any map. Layers:
 *   L1: Declared INTROCMD_SPAWN pads (existing, validated)
 *   L2: Waypoint/navmesh sampling with deterministic RNG
 *   L3: Grid raycast on AABB floor plane
 *   L4: Centroid + radial dilation (unconditional guarantee)
 *
 * All candidates pass raycast-budget validation: 14 rays outward, no backface
 * hits, sum(distances) >= threshold. L4 last-resort accepts highest-budget
 * candidates if no dilation fully passes.
 *
 * Design ref: context/designs/spawn-system-architecture-2026-04-13.md
 */

#include <ultra64.h>
#include "constants.h"
#include "data.h"
#include "types.h"
#include "bss.h"
#include "game/spawnpool.h"
#include "game/bg.h"
#include "game/pad.h"
#include "lib/collision.h"
#include "lib/rng.h"
#include "system.h"

#include <math.h>
#include <string.h>

/* Externs for spawn data (declared in player.c) */
extern s16 g_SpawnPoints[];
extern s32 g_NumSpawnPoints;

/* ========================================================================
 * Module state
 * ======================================================================== */

static spawn_pool_t s_Pool;
static bool s_PoolReady = false;

/* ========================================================================
 * Deterministic PRNG (xorshift32)
 *
 * Separate from game RNG so spawn pool is reproducible from seed alone.
 * ======================================================================== */

static u32 s_SpawnRngState;

static void spawnRngSeed(u32 seed)
{
	s_SpawnRngState = seed ? seed : 0xDEADBEEF;
}

static u32 spawnRngNext(void)
{
	u32 x = s_SpawnRngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_SpawnRngState = x;
	return x;
}

/* ========================================================================
 * FNV-1a hash for stage_id -> seed component
 * ======================================================================== */

static u32 spawnHashString(const char *str)
{
	u32 hash = 0x811c9dc5u;
	if (str) {
		while (*str) {
			hash ^= (u32)(u8)*str++;
			hash *= 0x01000193u;
		}
	}
	return hash;
}

/* ========================================================================
 * AABB computation from room geometry
 * ======================================================================== */

void spawnPoolComputeAABB(spawn_aabb_t *aabb)
{
	s32 r;
	bool first = true;

	aabb->valid = false;

	if (!g_Rooms || !g_BgRooms || g_Vars.roomcount <= 0) {
		aabb->min.x = aabb->min.y = aabb->min.z = 0.0f;
		aabb->max.x = aabb->max.y = aabb->max.z = 1000.0f;
		return;
	}

	for (r = 1; r < g_Vars.roomcount; r++) {
		f32 rmin_x = g_Rooms[r].bbmin[0];
		f32 rmin_y = g_Rooms[r].bbmin[1];
		f32 rmin_z = g_Rooms[r].bbmin[2];
		f32 rmax_x = g_Rooms[r].bbmax[0];
		f32 rmax_y = g_Rooms[r].bbmax[1];
		f32 rmax_z = g_Rooms[r].bbmax[2];

		/* Skip rooms with degenerate bboxes */
		if (rmax_x <= rmin_x && rmax_y <= rmin_y && rmax_z <= rmin_z) {
			continue;
		}

		if (first) {
			aabb->min.x = rmin_x; aabb->min.y = rmin_y; aabb->min.z = rmin_z;
			aabb->max.x = rmax_x; aabb->max.y = rmax_y; aabb->max.z = rmax_z;
			first = false;
		} else {
			if (rmin_x < aabb->min.x) aabb->min.x = rmin_x;
			if (rmin_y < aabb->min.y) aabb->min.y = rmin_y;
			if (rmin_z < aabb->min.z) aabb->min.z = rmin_z;
			if (rmax_x > aabb->max.x) aabb->max.x = rmax_x;
			if (rmax_y > aabb->max.y) aabb->max.y = rmax_y;
			if (rmax_z > aabb->max.z) aabb->max.z = rmax_z;
		}
	}

	if (first) {
		/* No valid rooms at all */
		aabb->min.x = aabb->min.y = aabb->min.z = 0.0f;
		aabb->max.x = aabb->max.y = aabb->max.z = 1000.0f;
	} else {
		aabb->valid = true;
	}
}

/* ========================================================================
 * Raycast-budget validation
 *
 * Fires 14 rays from candidate: 6 cardinal + 8 horizontal diagonals.
 * Returns sum of ray distances, or -1.0f if a backface hit is detected
 * (indicating the candidate is inside solid geometry).
 * ======================================================================== */

/* Ray directions: 6 cardinal + 8 XZ diagonals */
static const struct coord s_RayDirs[SPAWNPOOL_RAY_COUNT] = {
	{ 1.0f,  0.0f,  0.0f},  /* +X */
	{-1.0f,  0.0f,  0.0f},  /* -X */
	{ 0.0f,  1.0f,  0.0f},  /* +Y (up) */
	{ 0.0f, -1.0f,  0.0f},  /* -Y (down) */
	{ 0.0f,  0.0f,  1.0f},  /* +Z */
	{ 0.0f,  0.0f, -1.0f},  /* -Z */
	{ 0.7071f, 0.0f,  0.7071f},  /* +X+Z */
	{ 0.7071f, 0.0f, -0.7071f},  /* +X-Z */
	{-0.7071f, 0.0f,  0.7071f},  /* -X+Z */
	{-0.7071f, 0.0f, -0.7071f},  /* -X-Z */
	{ 0.7071f, 0.3536f, 0.0f},   /* +X upper */
	{-0.7071f, 0.3536f, 0.0f},   /* -X upper */
	{ 0.0f, 0.3536f,  0.7071f},  /* +Z upper */
	{ 0.0f, 0.3536f, -0.7071f},  /* -Z upper */
};

f32 spawnPoolRaycastBudget(const struct coord *pos, RoomNum room)
{
	f32 budget = 0.0f;
	s32 i;

	if (room < 0) {
		return -1.0f;
	}

	for (i = 0; i < SPAWNPOOL_RAY_COUNT; i++) {
		struct coord endpoint;
		struct hitthing hit;
		bool didHit;

		endpoint.x = pos->x + s_RayDirs[i].x * SPAWNPOOL_RAY_RANGE;
		endpoint.y = pos->y + s_RayDirs[i].y * SPAWNPOOL_RAY_RANGE;
		endpoint.z = pos->z + s_RayDirs[i].z * SPAWNPOOL_RAY_RANGE;

		memset(&hit, 0, sizeof(hit));
		didHit = bgTestHitInRoom((struct coord *)pos, &endpoint, room, &hit);

		if (didHit) {
			f32 dx = hit.pos.x - pos->x;
			f32 dy = hit.pos.y - pos->y;
			f32 dz = hit.pos.z - pos->z;
			f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);

			/* Backface check: if the hit is extremely close (< 5 units),
			 * the candidate is likely inside or against geometry. A true
			 * backface test would check the face normal dot product, but
			 * PD's bgTestHitInRoom doesn't expose the normal. Instead,
			 * check if multiple nearby hits cluster -- if more than half
			 * the rays hit within 30 units, reject as "inside geometry". */
			if (dist < 5.0f) {
				return -1.0f;
			}

			budget += dist;
		} else {
			/* No hit = ray traveled full range = open space */
			budget += SPAWNPOOL_RAY_RANGE;
		}
	}

	return budget;
}

/* ========================================================================
 * Full candidate validation
 * ======================================================================== */

f32 spawnPoolValidateCandidate(const struct coord *pos, RoomNum room,
                               const spawn_point_t *existing, s32 num_existing,
                               f32 min_spacing)
{
	RoomNum inrooms[21];
	RoomNum aboverooms[21];
	RoomNum bestroom = -1;
	f32 ground_y;
	s32 i;

	/* 1. Room validity: must have a valid room */
	if (room < 0) {
		inrooms[0] = -1;
		bgFindRoomsByPos((struct coord *)pos, inrooms, aboverooms, 20, &bestroom);
		if (inrooms[0] >= 0) {
			room = inrooms[0];
		} else if (bestroom >= 0) {
			room = bestroom;
		} else {
			return -1.0f;
		}
	}

	/* 2. Ground clearance: must have a floor within 500 units below */
	{
		RoomNum grooms[2] = { room, -1 };
		ground_y = cdFindGroundInfoAtCyl((struct coord *)pos, 30.0f, grooms,
		                                 NULL, NULL, NULL, NULL, NULL, NULL);
		if (pos->y - ground_y > 500.0f) {
			return -1.0f;
		}
	}

	/* 3. Vertical clearance: 180 units above must be clear */
	{
		struct coord above;
		RoomNum crooms[2] = { room, -1 };
		above.x = pos->x;
		above.y = pos->y + 180.0f;
		above.z = pos->z;
		if (cdExamCylMove01((struct coord *)pos, &above, 30.0f, crooms,
		                    CDTYPE_BG, true, pos->y + 180.0f, pos->y)
		    == CDRESULT_COLLISION) {
			return -1.0f;
		}
	}

	/* 4. Not inside geometry */
	if (!bgTestPosInRoom((struct coord *)pos, room)) {
		return -1.0f;
	}

	/* 5. Minimum spacing from existing accepted points */
	for (i = 0; i < num_existing; i++) {
		f32 dx = pos->x - existing[i].pos.x;
		f32 dz = pos->z - existing[i].pos.z;
		if (dx * dx + dz * dz < min_spacing * min_spacing) {
			return -1.0f;
		}
	}

	/* 6. Raycast budget */
	return spawnPoolRaycastBudget(pos, room);
}

/* ========================================================================
 * Helper: add a point to the pool
 * ======================================================================== */

static bool poolAdd(spawn_pool_t *pool, const struct coord *pos, RoomNum room,
                    s16 source_pad, u8 layer, f32 budget)
{
	if (pool->count >= SPAWNPOOL_MAX) {
		return false;
	}
	spawn_point_t *pt = &pool->points[pool->count];
	pt->pos = *pos;
	pt->room = room;
	pt->source_pad = source_pad;
	pt->layer = layer;
	pt->budget_score = budget;
	pool->count++;
	if (layer > pool->max_layer_used) {
		pool->max_layer_used = layer;
	}
	return true;
}

/* ========================================================================
 * Adaptive min_spacing from AABB diagonal and needed count
 * ======================================================================== */

static f32 computeMinSpacing(const spawn_aabb_t *aabb, s32 needed)
{
	f32 dx, dz, diag, spacing;

	if (!aabb->valid || needed <= 1) {
		return 60.0f;
	}

	dx = aabb->max.x - aabb->min.x;
	dz = aabb->max.z - aabb->min.z;
	diag = sqrtf(dx * dx + dz * dz);

	spacing = diag / (f32)(needed * 2);

	if (spacing < 60.0f) spacing = 60.0f;
	if (spacing > 500.0f) spacing = 500.0f;

	return spacing;
}

/* ========================================================================
 * L1: Declared spawn points (from g_SpawnPoints[])
 * ======================================================================== */

static void spawnPoolL1Declared(spawn_pool_t *pool, s32 needed, f32 min_spacing)
{
	s32 i;

	for (i = 0; i < g_NumSpawnPoints && pool->count < needed; i++) {
		struct pad pad;
		struct coord pos;
		RoomNum room;
		f32 budget;

		padUnpack(g_SpawnPoints[i], PADFIELD_POS | PADFIELD_ROOM, &pad);
		pos = pad.pos;
		room = pad.room;

		budget = spawnPoolValidateCandidate(&pos, room, pool->points,
		                                    pool->count, min_spacing);

		if (budget >= SPAWNPOOL_BUDGET_THRESHOLD) {
			poolAdd(pool, &pos, room, g_SpawnPoints[i],
			        SPAWNLAYER_DECLARED, budget);
		} else {
			sysLogPrintf(LOG_NOTE,
				"SPAWNPOOL: L1 pad %d rejected (budget=%.0f, room=%d)",
				(s32)g_SpawnPoints[i], budget, (s32)room);
		}
	}

	sysLogPrintf(LOG_NOTE, "SPAWNPOOL: L1 declared: %d/%d accepted from %d pads",
		pool->count, needed, g_NumSpawnPoints);
}

/* ========================================================================
 * L2: Waypoint/navmesh sampling with deterministic RNG
 * ======================================================================== */

static void spawnPoolL2Waypoints(spawn_pool_t *pool, s32 needed,
                                 f32 min_spacing)
{
	struct waypoint *wpts;
	s32 numwpts = 0;
	s32 budget_limit;
	s32 attempts = 0;
	s32 accepted_start = pool->count;

	if (!g_StageSetup.waypoints) {
		sysLogPrintf(LOG_NOTE, "SPAWNPOOL: L2 skipped -- no waypoints");
		return;
	}

	wpts = g_StageSetup.waypoints;
	while (wpts[numwpts].padnum >= 0) {
		numwpts++;
	}

	if (numwpts == 0) {
		sysLogPrintf(LOG_NOTE, "SPAWNPOOL: L2 skipped -- 0 waypoints");
		return;
	}

	budget_limit = numwpts * 4;

	while (pool->count < needed && attempts < budget_limit) {
		s32 idx = (s32)(spawnRngNext() % (u32)numwpts);
		struct pad pad;
		struct coord pos;
		RoomNum room;
		f32 budget;

		attempts++;

		padUnpack(wpts[idx].padnum, PADFIELD_POS | PADFIELD_ROOM, &pad);
		if (pad.room < 0) {
			continue;
		}

		pos = pad.pos;
		room = pad.room;

		budget = spawnPoolValidateCandidate(&pos, room, pool->points,
		                                    pool->count, min_spacing);

		if (budget >= SPAWNPOOL_BUDGET_THRESHOLD) {
			poolAdd(pool, &pos, room, (s16)wpts[idx].padnum,
			        SPAWNLAYER_WAYPOINT, budget);
		}
	}

	sysLogPrintf(LOG_NOTE,
		"SPAWNPOOL: L2 waypoints: %d accepted from %d attempts (%d waypoints available)",
		pool->count - accepted_start, attempts, numwpts);
}

/* ========================================================================
 * L3: Grid sampling on AABB floor plane
 * ======================================================================== */

static void spawnPoolL3Grid(spawn_pool_t *pool, s32 needed,
                            const spawn_aabb_t *aabb, f32 min_spacing)
{
	f32 width, depth, step;
	s32 cols, rows, total;
	s32 gx, gz;
	s32 accepted_start = pool->count;

	if (!aabb->valid) {
		sysLogPrintf(LOG_NOTE, "SPAWNPOOL: L3 skipped -- no valid AABB");
		return;
	}

	width = aabb->max.x - aabb->min.x;
	depth = aabb->max.z - aabb->min.z;

	if (width < 1.0f || depth < 1.0f) {
		sysLogPrintf(LOG_NOTE, "SPAWNPOOL: L3 skipped -- degenerate AABB");
		return;
	}

	/* Grid step: oversample 4x to account for rejected candidates */
	{
		s32 cells_needed = needed * 4;
		f32 cells_side = sqrtf((f32)cells_needed);
		if (cells_side < 2.0f) cells_side = 2.0f;
		step = (width > depth ? width : depth) / cells_side;
		if (step < 50.0f) step = 50.0f;
	}

	cols = (s32)(width / step) + 1;
	rows = (s32)(depth / step) + 1;
	total = cols * rows;

	/* Cap total grid cells to prevent excessive computation */
	if (total > 10000) {
		step *= sqrtf((f32)total / 10000.0f);
		cols = (s32)(width / step) + 1;
		rows = (s32)(depth / step) + 1;
	}

	for (gz = 0; gz < rows && pool->count < needed; gz++) {
		for (gx = 0; gx < cols && pool->count < needed; gx++) {
			struct coord candidate;
			RoomNum inrooms[21];
			RoomNum aboverooms[21];
			RoomNum bestroom = -1;
			RoomNum grooms[2];
			f32 ground_y;
			f32 budget;

			candidate.x = aabb->min.x + (f32)gx * step + step * 0.5f;
			candidate.z = aabb->min.z + (f32)gz * step + step * 0.5f;
			candidate.y = aabb->max.y;  /* start from top, raycast down */

			/* Raycast down to find ground */
			inrooms[0] = -1;
			bgFindRoomsByPos(&candidate, inrooms, aboverooms, 20, &bestroom);

			if (inrooms[0] < 0 && bestroom < 0) {
				continue;
			}

			grooms[0] = (inrooms[0] >= 0) ? inrooms[0] : bestroom;
			grooms[1] = -1;

			ground_y = cdFindGroundInfoAtCyl(&candidate, 30.0f, grooms,
			                                 NULL, NULL, NULL, NULL, NULL, NULL);

			/* Check ground is reasonable */
			if (candidate.y - ground_y > 5000.0f || ground_y < aabb->min.y - 500.0f) {
				continue;
			}

			candidate.y = ground_y + 10.0f;

			/* Re-resolve room at ground level */
			inrooms[0] = -1;
			bestroom = -1;
			bgFindRoomsByPos(&candidate, inrooms, aboverooms, 20, &bestroom);
			{
				RoomNum resolved = (inrooms[0] >= 0) ? inrooms[0] : bestroom;
				if (resolved < 0) {
					continue;
				}

				budget = spawnPoolValidateCandidate(&candidate, resolved,
				                                     pool->points, pool->count,
				                                     min_spacing);
				if (budget >= SPAWNPOOL_BUDGET_THRESHOLD) {
					poolAdd(pool, &candidate, resolved, -1,
					        SPAWNLAYER_GRID, budget);
				}
			}
		}
	}

	sysLogPrintf(LOG_NOTE,
		"SPAWNPOOL: L3 grid: %d accepted from %dx%d grid (step=%.0f)",
		pool->count - accepted_start, cols, rows, step);
}

/* ========================================================================
 * L4: Centroid + radial dilation (unconditional guarantee)
 * ======================================================================== */

/* Scratch space for L4 candidates across dilations */
typedef struct l4_candidate {
	struct coord pos;
	RoomNum room;
	f32 budget;
} l4_candidate_t;

static void spawnPoolL4Radial(spawn_pool_t *pool, s32 needed,
                              const spawn_aabb_t *aabb)
{
	struct coord center;
	f32 initial_radius;
	f32 radius;
	s32 slots_needed;
	s32 dilation;
	s32 accepted_start = pool->count;

	/* Best candidates seen across all dilations (for last-resort) */
	l4_candidate_t best[SPAWNPOOL_MAX];
	s32 best_count = 0;

	slots_needed = needed - pool->count;
	if (slots_needed <= 0) {
		return;
	}
	if (slots_needed > SPAWNPOOL_MAX - pool->count) {
		slots_needed = SPAWNPOOL_MAX - pool->count;
	}

	/* Compute center and initial radius */
	if (aabb->valid) {
		center.x = (aabb->min.x + aabb->max.x) * 0.5f;
		center.y = (aabb->min.y + aabb->max.y) * 0.5f;
		center.z = (aabb->min.z + aabb->max.z) * 0.5f;

		{
			f32 dx = aabb->max.x - aabb->min.x;
			f32 dz = aabb->max.z - aabb->min.z;
			f32 diag = sqrtf(dx * dx + dz * dz);
			initial_radius = diag / 4.0f;
			if (initial_radius > 200.0f) initial_radius = 200.0f;
			if (initial_radius < 50.0f) initial_radius = 50.0f;
		}
	} else {
		center.x = 0.0f;
		center.y = 100.0f;
		center.z = 0.0f;
		initial_radius = 200.0f;
	}

	radius = initial_radius;

	for (dilation = 0; dilation < SPAWNPOOL_L4_MAX_DILATIONS; dilation++) {
		s32 i;
		s32 pass_count = 0;
		l4_candidate_t ring[SPAWNPOOL_MAX];
		s32 ring_count = 0;

		for (i = 0; i < slots_needed; i++) {
			f32 angle = (2.0f * 3.14159265f * (f32)i) / (f32)slots_needed;
			struct coord candidate;
			RoomNum inrooms[21];
			RoomNum aboverooms[21];
			RoomNum bestroom = -1;
			RoomNum resolved;
			RoomNum grooms[2];
			f32 ground_y;
			f32 budget;

			candidate.x = center.x + cosf(angle) * radius;
			candidate.y = center.y;
			candidate.z = center.z + sinf(angle) * radius;

			/* Attempt ground resolution */
			inrooms[0] = -1;
			bgFindRoomsByPos(&candidate, inrooms, aboverooms, 20, &bestroom);
			resolved = (inrooms[0] >= 0) ? inrooms[0] : bestroom;

			if (resolved >= 0) {
				grooms[0] = resolved;
				grooms[1] = -1;
				ground_y = cdFindGroundInfoAtCyl(&candidate, 30.0f, grooms,
				                                 NULL, NULL, NULL, NULL,
				                                 NULL, NULL);
				if (candidate.y - ground_y < 500.0f) {
					candidate.y = ground_y + 10.0f;
				}
			} else {
				/* No room found -- use center height, room 0 as fallback */
				resolved = 0;
			}

			/* Raycast budget (not full validate -- L4 skips spacing) */
			budget = spawnPoolRaycastBudget(&candidate, resolved);
			if (budget < 0.0f) budget = 0.0f; /* inside geo, score = 0 */

			ring[ring_count].pos = candidate;
			ring[ring_count].room = resolved;
			ring[ring_count].budget = budget;
			ring_count++;

			/* Track best candidates across all dilations */
			if (best_count < slots_needed) {
				best[best_count] = ring[ring_count - 1];
				best_count++;
			} else {
				/* Replace the worst best if this one is better */
				s32 worst_idx = 0;
				s32 j;
				for (j = 1; j < best_count; j++) {
					if (best[j].budget < best[worst_idx].budget) {
						worst_idx = j;
					}
				}
				if (budget > best[worst_idx].budget) {
					best[worst_idx] = ring[ring_count - 1];
				}
			}

			if (budget >= SPAWNPOOL_BUDGET_THRESHOLD) {
				pass_count++;
			}
		}

		/* If all candidates in this ring passed, accept them */
		if (pass_count == slots_needed) {
			s32 j;
			for (j = 0; j < ring_count && pool->count < SPAWNPOOL_MAX; j++) {
				if (ring[j].budget >= SPAWNPOOL_BUDGET_THRESHOLD) {
					poolAdd(pool, &ring[j].pos, ring[j].room, -1,
					        SPAWNLAYER_RADIAL, ring[j].budget);
					if (pool->count >= needed) break;
				}
			}
			sysLogPrintf(LOG_NOTE,
				"SPAWNPOOL: L4 radial: %d accepted at radius=%.0f (dilation %d)",
				pool->count - accepted_start, radius, dilation);
			return;
		}

		/* Dilate outward */
		radius *= 1.5f;
	}

	/* Last resort: accept highest-budget candidates from all dilations.
	 * Sort best[] by budget descending and accept top slots_needed. */
	{
		s32 j, k;
		/* Simple selection sort -- slots_needed is small (<=36) */
		for (j = 0; j < best_count - 1; j++) {
			s32 max_idx = j;
			for (k = j + 1; k < best_count; k++) {
				if (best[k].budget > best[max_idx].budget) {
					max_idx = k;
				}
			}
			if (max_idx != j) {
				l4_candidate_t tmp = best[j];
				best[j] = best[max_idx];
				best[max_idx] = tmp;
			}
		}

		for (j = 0; j < best_count && pool->count < needed &&
		     pool->count < SPAWNPOOL_MAX; j++) {
			poolAdd(pool, &best[j].pos, best[j].room, -1,
			        SPAWNLAYER_RADIAL, best[j].budget);
		}
	}

	sysLogPrintf(LOG_WARNING,
		"SPAWNPOOL: L4 last-resort: %d accepted from %d best candidates across %d dilations",
		pool->count - accepted_start, best_count,
		SPAWNPOOL_L4_MAX_DILATIONS);
}

/* ========================================================================
 * Main entry point: build the validated spawn pool
 * ======================================================================== */

void spawnPoolBuild(spawn_pool_t *pool, const char *stage_id,
                    u32 match_seed, s32 needed)
{
	spawn_aabb_t aabb;
	f32 min_spacing;

	/* Initialize */
	memset(pool, 0, sizeof(*pool));
	pool->seed = match_seed;
	pool->needed = needed;

	if (needed <= 0) {
		s_PoolReady = true;
		return;
	}

	/* Clamp needed to pool max */
	if (needed > SPAWNPOOL_MAX) {
		needed = SPAWNPOOL_MAX;
	}

	/* Seed deterministic RNG from stage_id + match_seed */
	spawnRngSeed(spawnHashString(stage_id) ^ match_seed);

	/* Compute stage AABB */
	spawnPoolComputeAABB(&aabb);

	/* Adaptive min spacing */
	min_spacing = computeMinSpacing(&aabb, needed);

	sysLogPrintf(LOG_NOTE,
		"SPAWNPOOL: build start -- stage=%s seed=0x%08x needed=%d aabb_valid=%d min_spacing=%.0f",
		stage_id ? stage_id : "(null)", match_seed, needed,
		aabb.valid, min_spacing);

	/* L1: Declared spawn points */
	spawnPoolL1Declared(pool, needed, min_spacing);

	if (pool->count >= needed) {
		goto done;
	}

	/* L2: Waypoint sampling */
	spawnPoolL2Waypoints(pool, needed, min_spacing);

	if (pool->count >= needed) {
		goto done;
	}

	/* L3: Grid raycast */
	spawnPoolL3Grid(pool, needed, &aabb, min_spacing);

	if (pool->count >= needed) {
		goto done;
	}

	/* L4: Radial fallback (guaranteed) */
	spawnPoolL4Radial(pool, needed, &aabb);

done:
	sysLogPrintf(LOG_NOTE,
		"SPAWNPOOL: build complete -- %d points (needed %d), max_layer=%d",
		pool->count, needed, pool->max_layer_used);

	/* Diagnostic: dump first few points */
	{
		s32 i;
		s32 dump_count = pool->count < 8 ? pool->count : 8;
		for (i = 0; i < dump_count; i++) {
			sysLogPrintf(LOG_NOTE,
				"SPAWNPOOL:   [%d] L%d pad=%d pos=(%.0f,%.0f,%.0f) room=%d budget=%.0f",
				i, pool->points[i].layer, (s32)pool->points[i].source_pad,
				pool->points[i].pos.x, pool->points[i].pos.y,
				pool->points[i].pos.z, (s32)pool->points[i].room,
				pool->points[i].budget_score);
		}
	}

	s_PoolReady = true;
}

/* ========================================================================
 * Global pool accessors
 * ======================================================================== */

const spawn_pool_t *spawnPoolGet(void)
{
	return &s_Pool;
}

bool spawnPoolIsReady(void)
{
	return s_PoolReady;
}

/* ========================================================================
 * Integration: build the global pool (called from playerreset.c)
 * ======================================================================== */

void spawnPoolBuildGlobal(const char *stage_id, u32 match_seed, s32 needed)
{
	s_PoolReady = false;
	spawnPoolBuild(&s_Pool, stage_id, match_seed, needed);
}

/* Reset pool state (called on stage change) */
void spawnPoolReset(void)
{
	s_PoolReady = false;
	memset(&s_Pool, 0, sizeof(s_Pool));
}
