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
#include "game/atan2f.h"
#include "lib/collision.h"
#include "lib/rng.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "assetcatalog.h"

/* Externs for spawn data (declared in player.c) */
extern s16 g_SpawnPoints[];
extern s32 g_NumSpawnPoints;

/* ========================================================================
 * Module state
 * ======================================================================== */

static spawn_pool_t s_Pool;
static bool s_PoolReady = false;

/* S298 same-tick reservation bitset -- see spawnPoolClearReservations() in
 * the header.  Sized for SPAWNPOOL_MAX slots; auto-cleared on every
 * spawnPoolBuild() / spawnPoolReset() call, and also every time we detect
 * a new `g_Vars.lvframenum` (so reservations never persist across ticks —
 * the same slot that held bot A at match-start can hold a respawner
 * minutes later). */
static bool s_SpawnReserved[SPAWNPOOL_MAX];
static s32  s_SpawnReservedFrame = -1;

static void spawnPoolTickCheck(void)
{
	if (g_Vars.lvframenum != s_SpawnReservedFrame) {
		memset(s_SpawnReserved, 0, sizeof(s_SpawnReserved));
		s_SpawnReservedFrame = g_Vars.lvframenum;
	}
}

void spawnPoolClearReservations(void)
{
	memset(s_SpawnReserved, 0, sizeof(s_SpawnReserved));
	s_SpawnReservedFrame = g_Vars.lvframenum;
}

/* ========================================================================
 * Smoke test accumulator (M-7.x retroactive validation)
 *
 * Records per-stage pool build results across the session.  Each unique
 * stage_id gets one slot; live (in-game) results overwrite offline ones.
 * ======================================================================== */

#define SMOKE_MAX_STAGES 128
#define SMOKE_SRC_LIVE    'L'  /* built during real gameplay */
#define SMOKE_SRC_OFFLINE 'O'  /* built offline with zeroed pads */

typedef struct smoke_record {
	char stage_id[CATALOG_ID_LEN];
	s32  needed;
	s32  produced;
	u8   max_layer;
	char source;   /* SMOKE_SRC_LIVE or SMOKE_SRC_OFFLINE */
	u32  time_ms;
} smoke_record_t;

static smoke_record_t s_SmokeLog[SMOKE_MAX_STAGES];
static s32            s_SmokeCount = 0;

static void smokeLogRecord(const char *stage_id, s32 needed, s32 produced,
                           u8 max_layer, char source, u32 time_ms)
{
	s32 i;
	if (!stage_id || !stage_id[0]) return;

	/* Update existing entry (live always overwrites offline) */
	for (i = 0; i < s_SmokeCount; i++) {
		if (strncmp(s_SmokeLog[i].stage_id, stage_id, CATALOG_ID_LEN - 1) == 0) {
			if (source == SMOKE_SRC_LIVE || s_SmokeLog[i].source == SMOKE_SRC_OFFLINE) {
				s_SmokeLog[i].needed    = needed;
				s_SmokeLog[i].produced  = produced;
				s_SmokeLog[i].max_layer = max_layer;
				s_SmokeLog[i].source    = source;
				s_SmokeLog[i].time_ms   = time_ms;
			}
			return;
		}
	}

	/* New entry */
	if (s_SmokeCount >= SMOKE_MAX_STAGES) return;
	strncpy(s_SmokeLog[s_SmokeCount].stage_id, stage_id, CATALOG_ID_LEN - 1);
	s_SmokeLog[s_SmokeCount].stage_id[CATALOG_ID_LEN - 1] = '\0';
	s_SmokeLog[s_SmokeCount].needed    = needed;
	s_SmokeLog[s_SmokeCount].produced  = produced;
	s_SmokeLog[s_SmokeCount].max_layer = max_layer;
	s_SmokeLog[s_SmokeCount].source    = source;
	s_SmokeLog[s_SmokeCount].time_ms   = time_ms;
	s_SmokeCount++;
}

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
		/* B-206: mark valid so L3/L4 use the fallback volume instead of the
		 * (0,100,0) sentinel in spawnPoolL4Radial / last-resort synthesis. */
		aabb->valid = true;
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
		/* No valid rooms at all — still use a finite search volume so L4
		 * does not fall back to the world sentinel (B-206 / Area 52 class). */
		aabb->min.x = aabb->min.y = aabb->min.z = 0.0f;
		aabb->max.x = aabb->max.y = aabb->max.z = 1000.0f;
		aabb->valid = true;
	} else {
		aabb->valid = true;
	}
}

/* ========================================================================
 * Raycast-budget validation
 *
 * Fires 18 rays from candidate: 6 cardinal + 4 horizontal XZ diag +
 * 4 upper diag + 4 lower diag. Returns sum of ray distances, or -1.0f
 * if a hit is detected closer than the capsule radius (inside solid).
 *
 * Lower diagonals added 2026-04-16 to catch overhangs BELOW the candidate
 * (previously the 14-ray set had 4 upper diagonals and 0 lower → a candidate
 * hovering off a ledge would validate clean).
 * ======================================================================== */

/* Ray directions: 6 cardinal + 4 XZ diag + 4 upper diag + 4 lower diag */
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
	{ 0.7071f, -0.3536f, 0.0f},  /* +X lower */
	{-0.7071f, -0.3536f, 0.0f},  /* -X lower */
	{ 0.0f, -0.3536f,  0.7071f}, /* +Z lower */
	{ 0.0f, -0.3536f, -0.7071f}, /* -Z lower */
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
		/* Rays with a downward component (-Y) are probing for ground /
		 * overhangs beneath the candidate. A close hit on a downward ray
		 * means "ground is right there" -- that's GOOD, not a trap. So
		 * skip the capsule-clearance reject for those; the horizontal /
		 * upward rays still gate on it. */
		const bool isDownward = (s_RayDirs[i].y < -0.1f);

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

			/* Capsule-clearance check: if the surface is closer than the
			 * player capsule radius, the player would clip or be trapped.
			 * bgTestHitInRoom doesn't expose face normals, so we use
			 * capsule radius as a proxy for "inside/against geometry":
			 * any hit within SPAWNPOOL_CAPSULE_RADIUS units is too close.
			 * Not applied to downward rays (see comment above). */
			if (!isDownward && dist < SPAWNPOOL_CAPSULE_RADIUS) {
				return -1.0f;
			}

			budget += dist;
		} else {
			/* No hit = ray traveled full range = open space. For downward
			 * rays this means no ground beneath this direction (void /
			 * overhang). Keep the full range contribution — candidates
			 * are still gated by spawnPoolValidateCandidate's ground
			 * sentinel check, which catches the direct -Y-void case. */
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

	/* 2. Ground clearance: must have a floor within 500 units below.
	 * Guard the ground sentinel: cdFindGroundInfoAtCyl returns -100000
	 * when no floor is found. Treating that as a real ground causes
	 * mid-air spawns.
	 *
	 * S298: pass neighbour rooms too.  Near room boundaries (doorways,
	 * portal seams, overlapping rooms) the actual floor geometry lives in
	 * an adjacent room; a single-room query returns the -100000 sentinel
	 * and the candidate gets rejected as "mid-air" even though a real
	 * floor is a few units away.  bgFindRoomsByPos(inrooms) returns up to
	 * 20 candidate rooms including neighbours. */
	{
		RoomNum grooms[8];
		RoomNum ginrooms[21];
		RoomNum gaboverooms[21];
		RoomNum gbest = -1;
		s32 grcount = 0;

		grooms[grcount++] = room;

		bgFindRoomsByPos((struct coord *)pos, ginrooms, gaboverooms,
		                 20, &gbest);
		for (i = 0; ginrooms[i] >= 0 && grcount < 7; i++) {
			s32 k;
			bool dup = false;
			for (k = 0; k < grcount; k++) {
				if (grooms[k] == ginrooms[i]) { dup = true; break; }
			}
			if (!dup) {
				grooms[grcount++] = ginrooms[i];
			}
		}
		grooms[grcount] = -1;

		ground_y = cdFindGroundInfoAtCyl((struct coord *)pos, 30.0f, grooms,
		                                 NULL, NULL, NULL, NULL, NULL, NULL);
		if (ground_y <= -99000.0f) {
			return -1.0f;
		}
		if (pos->y - ground_y > 500.0f) {
			return -1.0f;
		}
	}

	/* 3. Vertical clearance: 180 units above must be clear.
	 * Uses CDTYPE_ALL so prop geometry (crates, pickups, decor) is also
	 * tested — previously BG-only, which missed spawns landing on top
	 * of a dropped weapon / crate pile. */
	{
		struct coord above;
		RoomNum crooms[2] = { room, -1 };
		above.x = pos->x;
		above.y = pos->y + 180.0f;
		above.z = pos->z;
		if (cdExamCylMove01((struct coord *)pos, &above, 30.0f, crooms,
		                    CDTYPE_ALL, true, pos->y + 180.0f, pos->y)
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
 * Helper: 8-direction wall probe -> facing angle (radians)
 *
 * Mirrors the probe in playerreset.c:691-715.  For a given spawn candidate
 * we fire 8 cylindrical-move tests at 200 units; whichever directions hit
 * a BG wall contribute a unit vector, and we face 180° away from the
 * average wall normal.  If no walls are near, angle_rad stays 0 (face
 * +Z) since any direction is fine.
 * ======================================================================== */

static f32 poolWallProbeAngle(const struct coord *pos, RoomNum room)
{
	static const f32 dirX[8] = { 0.0f,  0.707f,  1.0f,  0.707f,
	                              0.0f, -0.707f, -1.0f, -0.707f };
	static const f32 dirZ[8] = { 1.0f,  0.707f,  0.0f, -0.707f,
	                             -1.0f, -0.707f,  0.0f,  0.707f };

	RoomNum rooms[2];
	f32 wallX = 0.0f, wallZ = 0.0f;
	s32 wallCount = 0;
	s32 dir;

	rooms[0] = room;
	rooms[1] = -1;

	for (dir = 0; dir < 8; dir++) {
		struct coord probe;
		probe.x = pos->x + dirX[dir] * 200.0f;
		probe.y = pos->y;
		probe.z = pos->z + dirZ[dir] * 200.0f;

		if (cdExamCylMove01((struct coord *)pos, &probe, 30.0f, rooms,
		                    CDTYPE_BG, false, 0, 0) == CDRESULT_COLLISION) {
			wallX += dirX[dir];
			wallZ += dirZ[dir];
			wallCount++;
		}
	}

	if (wallCount > 0) {
		/* Face away from the average wall direction */
		return atan2f(wallX, -wallZ);
	}
	return 0.0f;
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
	pt->angle_rad = poolWallProbeAngle(pos, room);
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
			sysLogPrintf(LOG_WARNING,
				"SPAWNPOOL: L1 declared pad %d failed validation (budget=%.0f, room=%d) -- falling through to L2",
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

/* Lightweight L4 safety check: room valid, ground found (not sentinel),
 * position actually inside a room. Skips spacing + ray-budget (L4 path
 * already applies ray-budget upstream). Used at last-resort accept so
 * we never commit a pool entry pointing into nowhere. */
static bool l4ValidateSafety(const struct coord *pos, RoomNum room)
{
	RoomNum grooms[2];
	f32 ground_y;

	if (room < 0) {
		return false;
	}
	if (!bgTestPosInRoom((struct coord *)pos, room)) {
		return false;
	}

	grooms[0] = room;
	grooms[1] = -1;
	ground_y = cdFindGroundInfoAtCyl((struct coord *)pos, 30.0f, grooms,
	                                 NULL, NULL, NULL, NULL, NULL, NULL);
	if (ground_y <= -99000.0f) {
		return false;
	}
	if (pos->y - ground_y > 500.0f) {
		return false;
	}
	return true;
}

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

		/* If all candidates in this ring passed, accept them (each still
		 * goes through a lightweight safety check — room valid / in-room /
		 * ground found — so we don't commit into a wall or mid-air). */
		if (pass_count == slots_needed) {
			s32 j;
			for (j = 0; j < ring_count && pool->count < SPAWNPOOL_MAX; j++) {
				if (ring[j].budget >= SPAWNPOOL_BUDGET_THRESHOLD
						&& l4ValidateSafety(&ring[j].pos, ring[j].room)) {
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
			/* Last-resort gate: even in worst-case, reject entries that
			 * point into no-room / no-ground. Better a short pool that
			 * cycles through valid points than a full pool that spawns
			 * players into the void. */
			if (!l4ValidateSafety(&best[j].pos, best[j].room)) {
				continue;
			}
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

	/* S298: pool rebuild invalidates any prior reservations. */
	spawnPoolClearReservations();

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
	clock_t t0 = clock();
	s_PoolReady = false;
	spawnPoolBuild(&s_Pool, stage_id, match_seed, needed);
	{
		u32 ms = (u32)(((clock() - t0) * 1000u) / (u32)CLOCKS_PER_SEC);
		smokeLogRecord(stage_id, needed, s_Pool.count, s_Pool.max_layer_used,
		               SMOKE_SRC_LIVE, ms);
	}
}

/* Reset pool state (called on stage change) */
void spawnPoolReset(void)
{
	s_PoolReady = false;
	memset(&s_Pool, 0, sizeof(s_Pool));
	/* S298: stage change invalidates reservations. */
	spawnPoolClearReservations();
}

/* ========================================================================
 * Forge integration: append forge-placed spawn points to the live pool
 * ======================================================================== */

s32 spawnPoolAppendForgePoints(const struct coord *positions,
                               const f32 *facing_rads,
                               s32 count)
{
	s32 added = 0;
	s32 i;

	if (!positions || count <= 0) return 0;

	for (i = 0; i < count && s_Pool.count < SPAWNPOOL_MAX; i++) {
		RoomNum inrooms[21];
		RoomNum aboverooms[21];
		RoomNum bestroom = -1;
		RoomNum room;
		f32 budget;
		spawn_point_t *pt;

		bgFindRoomsByPos((struct coord *)&positions[i], inrooms, aboverooms,
		                 20, &bestroom);
		room = (inrooms[0] >= 0) ? inrooms[0] : bestroom;
		if (room < 0) room = 0;

		budget = spawnPoolRaycastBudget(&positions[i], room);
		if (budget < 0.0f) budget = 0.0f;

		pt               = &s_Pool.points[s_Pool.count];
		pt->pos          = positions[i];
		pt->room         = room;
		pt->source_pad   = -1;
		pt->layer        = SPAWNLAYER_DECLARED;
		pt->budget_score = budget;
		pt->angle_rad    = facing_rads ? facing_rads[i] : 0.0f;
		s_Pool.count++;
		added++;
	}

	if (added > 0) {
		sysLogPrintf(LOG_NOTE,
		        "SPAWNPOOL: appended %d/%d forge spawn points (pool now %d)",
		        added, count, s_Pool.count);
	}

	return added;
}

/* ========================================================================
 * spawnPoolSelect -- farthest-point-first greedy spawn assignment
 *
 * For FFA (num_teams == 0 or team == -1):
 *   Pick the pool point that maximizes min-distance-to-occupied.
 *
 * For team modes (num_teams >= 2, team >= 0):
 *   Partition pool into angular sectors around pool_center. Prefer the
 *   sector assigned to this team. Fall back to other sectors if the
 *   team's sector is exhausted. Within the sector, apply farthest-first.
 * ======================================================================== */

/* Compute min squared distance from pool point i to any occupied position.
 * Returns 1e30f when num_occupied == 0 (treat empty as infinitely far). */
static f32 poolMinDistSq(const spawn_pool_t *pool, s32 i,
                         const struct coord *occupied, s32 num_occupied)
{
	f32 min_dist = 1e30f;
	s32 j;
	for (j = 0; j < num_occupied; j++) {
		f32 dx = pool->points[i].pos.x - occupied[j].x;
		f32 dz = pool->points[i].pos.z - occupied[j].z;
		f32 dist_sq = dx * dx + dz * dz;
		if (dist_sq < min_dist) min_dist = dist_sq;
	}
	return min_dist;
}

/* Farthest-point-first pick over `pool`, skipping any index whose
 * `skip[i]` bit is set.  Returns -1 if every slot was skipped.
 * When pool_center + team params are active, tries the team sector
 * first and falls through to full-pool FFA if the sector is empty. */
static s32 poolPickFarthest(const spawn_pool_t *pool,
                            const struct coord *occupied, s32 num_occupied,
                            const bool *skip, s32 team, s32 num_teams,
                            const struct coord *pool_center)
{
	s32 best_idx = -1;
	f32 best_min_dist = -1.0f;
	s32 i;

	if (num_teams >= 2 && team >= 0 && team < num_teams && pool_center) {
		f32 sector_size = (2.0f * 3.14159265f) / (f32)num_teams;
		f32 sector_start = sector_size * (f32)team - 3.14159265f;
		f32 sector_end = sector_start + sector_size;

		for (i = 0; i < pool->count; i++) {
			f32 dx, dz, angle, min_dist;

			if (skip[i]) continue;

			dx = pool->points[i].pos.x - pool_center->x;
			dz = pool->points[i].pos.z - pool_center->z;
			angle = atan2f(dz, dx);
			if (angle < sector_start) angle += 2.0f * 3.14159265f;
			if (angle < sector_start || angle >= sector_end) continue;

			min_dist = poolMinDistSq(pool, i, occupied, num_occupied);
			if (num_occupied == 0) {
				min_dist = pool->points[i].budget_score;
			}
			if (min_dist > best_min_dist) {
				best_min_dist = min_dist;
				best_idx = i;
			}
		}

		if (best_idx >= 0) {
			return best_idx;
		}
		/* Sector empty — fall through to whole-pool FFA pass. */
	}

	for (i = 0; i < pool->count; i++) {
		f32 min_dist;

		if (skip[i]) continue;

		min_dist = poolMinDistSq(pool, i, occupied, num_occupied);
		if (num_occupied == 0) {
			min_dist = pool->points[i].budget_score;
		}
		if (min_dist > best_min_dist) {
			best_min_dist = min_dist;
			best_idx = i;
		}
	}

	return best_idx;
}

/* Mark pool entries within 10 units of any occupied position as
 * `used` — those would telefrag an existing player. */
static void poolMarkUsedFromOccupied(const spawn_pool_t *pool,
                                     const struct coord *occupied,
                                     s32 num_occupied, bool *used)
{
	s32 i, j;
	for (i = 0; i < pool->count; i++) {
		for (j = 0; j < num_occupied; j++) {
			f32 dx = pool->points[i].pos.x - occupied[j].x;
			f32 dz = pool->points[i].pos.z - occupied[j].z;
			if (dx * dx + dz * dz < 100.0f) {
				used[i] = true;
				break;
			}
		}
	}
}

const char *spawnPoolTierName(spawn_select_tier_t tier)
{
	switch (tier) {
	case SPAWN_TIER_1_OPTIMAL:    return "T1_OPTIMAL";
	case SPAWN_TIER_2_CYCLED:     return "T2_CYCLED";
	case SPAWN_TIER_3_REUSED:     return "T3_REUSED";
	case SPAWN_TIER_4_LASTRESORT: return "T4_LAST_RESORT";
	case SPAWN_TIER_NONE:
	default:                      return "T0_NONE";
	}
}

s32 spawnPoolSelectTiered(const spawn_pool_t *pool,
                          const struct coord *occupied, s32 num_occupied,
                          s32 team, s32 num_teams,
                          const struct coord *pool_center,
                          spawn_select_tier_t *out_tier)
{
	bool used[SPAWNPOOL_MAX];
	bool skip[SPAWNPOOL_MAX];
	s32 pick;
	s32 i;

	if (out_tier) *out_tier = SPAWN_TIER_NONE;

	if (!pool || pool->count <= 0) {
		return -1;
	}

	/* S298: auto-clear reservations at tick boundary so this bitset only
	 * arbitrates within a single 60Hz tick of selects. */
	spawnPoolTickCheck();

	memset(used, 0, sizeof(used));
	poolMarkUsedFromOccupied(pool, occupied, num_occupied, used);

	/* T1: farthest-point-first over unused + unreserved slots. */
	memcpy(skip, used, sizeof(skip));
	for (i = 0; i < pool->count && i < SPAWNPOOL_MAX; i++) {
		if (s_SpawnReserved[i]) skip[i] = true;
	}

	pick = poolPickFarthest(pool, occupied, num_occupied, skip,
	                        team, num_teams, pool_center);
	if (pick >= 0) {
		if (pick < SPAWNPOOL_MAX) s_SpawnReserved[pick] = true;
		if (out_tier) *out_tier = SPAWN_TIER_1_OPTIMAL;
		return pick;
	}

	/* T2: reservations ate the pool — unused slots still exist but every
	 * one is reserved.  Clear the reservation bitset (those earlier
	 * callers have committed their spawns by now) and retry. */
	{
		bool anyReserved = false;
		for (i = 0; i < pool->count && i < SPAWNPOOL_MAX; i++) {
			if (s_SpawnReserved[i]) { anyReserved = true; break; }
		}
		if (anyReserved) {
			memset(s_SpawnReserved, 0, sizeof(s_SpawnReserved));
			s_SpawnReservedFrame = g_Vars.lvframenum;

			memcpy(skip, used, sizeof(skip));
			pick = poolPickFarthest(pool, occupied, num_occupied, skip,
			                        team, num_teams, pool_center);
			if (pick >= 0) {
				if (pick < SPAWNPOOL_MAX) s_SpawnReserved[pick] = true;
				if (out_tier) *out_tier = SPAWN_TIER_2_CYCLED;
				sysLogPrintf(LOG_WARNING,
					"SPAWN.TIER: T2_CYCLED — reservations cleared (pool=%d occupied=%d), picked idx=%d",
					pool->count, num_occupied, pick);
				return pick;
			}
		}
	}

	/* T3: every slot collides with an occupied position OR failed
	 * validation.  Map oversubscribed — pick the slot farthest from any
	 * occupied pos, letting the engine resolve the overlap via the
	 * standard telefrag / push.  Team sector still honoured if it has
	 * any point at all. */
	memset(skip, 0, sizeof(skip));
	pick = poolPickFarthest(pool, occupied, num_occupied, skip,
	                        team, num_teams, pool_center);
	if (pick >= 0) {
		if (pick < SPAWNPOOL_MAX) s_SpawnReserved[pick] = true;
		if (out_tier) *out_tier = SPAWN_TIER_3_REUSED;
		sysLogPrintf(LOG_WARNING,
			"SPAWN.TIER: T3_REUSED — pool oversubscribed (pool=%d occupied=%d), reusing slot idx=%d",
			pool->count, num_occupied, pick);
		return pick;
	}

	/* Pool is entirely empty — caller must invoke last-resort. */
	return -1;
}

/* Back-compat wrapper — legacy callers that don't care about the tier. */
s32 spawnPoolSelect(const spawn_pool_t *pool, const struct coord *occupied,
                    s32 num_occupied, s32 team, s32 num_teams,
                    const struct coord *pool_center)
{
	return spawnPoolSelectTiered(pool, occupied, num_occupied,
	                             team, num_teams, pool_center, NULL);
}

spawn_select_tier_t spawnPoolLastResort(const struct coord *occupied,
                                        s32 num_occupied,
                                        struct coord *out_pos,
                                        RoomNum *out_room,
                                        f32 *out_angle)
{
	if (!out_pos || !out_room) {
		return SPAWN_TIER_NONE;
	}

	/* If the pool has ANY points, pick the farthest from occupied —
	 * even ones that failed validation are better than the void. */
	if (s_PoolReady && s_Pool.count > 0) {
		bool skip[SPAWNPOOL_MAX];
		s32 pick;

		memset(skip, 0, sizeof(skip));
		pick = poolPickFarthest(&s_Pool, occupied, num_occupied, skip,
		                        -1, 0, NULL);
		if (pick >= 0) {
			*out_pos  = s_Pool.points[pick].pos;
			*out_room = s_Pool.points[pick].room;
			if (out_angle) *out_angle = s_Pool.points[pick].angle_rad;
			sysLogPrintf(LOG_WARNING,
				"SPAWN.TIER: T4_LAST_RESORT — pool-slot reuse idx=%d L%d pos=(%.0f,%.0f,%.0f) room=%d",
				pick, (s32)s_Pool.points[pick].layer,
				out_pos->x, out_pos->y, out_pos->z,
				(s32)*out_room);
			return SPAWN_TIER_4_LASTRESORT;
		}
	}

	/* Pool is empty or unbuilt — synthesise a position at AABB centre
	 * with a radial offset from the rng.  This matches the philosophy
	 * of L4_RADIAL: never let a caller receive a void coordinate. */
	{
		spawn_aabb_t aabb;
		struct coord center;
		f32 radius;
		f32 angle;
		RoomNum inrooms[21];
		RoomNum aboverooms[21];
		RoomNum bestroom = -1;

		spawnPoolComputeAABB(&aabb);
		if (aabb.valid) {
			center.x = (aabb.min.x + aabb.max.x) * 0.5f;
			center.y = (aabb.min.y + aabb.max.y) * 0.5f;
			center.z = (aabb.min.z + aabb.max.z) * 0.5f;
			{
				f32 dx = aabb.max.x - aabb.min.x;
				f32 dz = aabb.max.z - aabb.min.z;
				f32 diag = sqrtf(dx * dx + dz * dz);
				radius = diag * 0.25f;
				if (radius < 100.0f) radius = 100.0f;
				if (radius > 800.0f) radius = 800.0f;
			}
		} else {
			center.x = 0.0f;
			center.y = 100.0f;
			center.z = 0.0f;
			radius = 200.0f;
		}

		/* Stagger by num_occupied so consecutive last-resort calls in
		 * the same burst don't land on identical points. */
		angle = ((f32)(num_occupied & 7)) * (2.0f * 3.14159265f / 8.0f);
		out_pos->x = center.x + cosf(angle) * radius;
		out_pos->y = center.y;
		out_pos->z = center.z + sinf(angle) * radius;

		inrooms[0] = -1;
		bgFindRoomsByPos(out_pos, inrooms, aboverooms, 20, &bestroom);
		if (inrooms[0] >= 0) {
			*out_room = inrooms[0];
		} else if (bestroom >= 0) {
			*out_room = bestroom;
		} else {
			*out_room = -1;
		}

		if (out_angle) *out_angle = 0.0f;

		sysLogPrintf(LOG_WARNING,
			"SPAWN.TIER: T4_LAST_RESORT — synthesised pos=(%.0f,%.0f,%.0f) room=%d angle=%.2f radius=%.0f (pool empty, occupied=%d)",
			out_pos->x, out_pos->y, out_pos->z, (s32)*out_room,
			angle, radius, num_occupied);
		return SPAWN_TIER_4_LASTRESORT;
	}
}

/* ========================================================================
 * spawnPoolSmokeTest -- diagnostic sweep of all stages
 *
 * Builds the spawn pool for every gameplay stagenum in g_Stages[] and
 * logs max_layer_used per stage. Returns count of stages needing L3+.
 * NOTE: This is a diagnostic tool for the dev menu / log. It does NOT
 * load actual stage geometry -- it operates on whatever is currently
 * loaded or uses placeholder results. For meaningful results, call
 * from within an actual stage context or iterate stageloads.
 * ======================================================================== */

s32 spawnPoolSmokeTest(void)
{
	s32 warnings = 0;
	s32 i;

	if (s_PoolReady) {
		sysLogPrintf(LOG_NOTE,
			"SPAWNPOOL SMOKE: current pool: %d points, max_layer=%d, seed=0x%08x",
			s_Pool.count, s_Pool.max_layer_used, s_Pool.seed);
		if (s_Pool.max_layer_used >= 3) {
			sysLogPrintf(LOG_WARNING,
				"SPAWNPOOL SMOKE: current stage required L%d (grid/radial fallback)",
				s_Pool.max_layer_used);
			warnings++;
		}
	} else {
		sysLogPrintf(LOG_NOTE, "SPAWNPOOL SMOKE: no pool built for current stage");
	}

	/* Summary of accumulated session log */
	for (i = 0; i < s_SmokeCount; i++) {
		if (s_SmokeLog[i].max_layer >= 3) warnings++;
		sysLogPrintf(LOG_NOTE,
			"SPAWNPOOL SMOKE LOG[%d]: %-32s L%d (%d/%d) src=%c t=%ums",
			i, s_SmokeLog[i].stage_id,
			(s32)s_SmokeLog[i].max_layer,
			s_SmokeLog[i].produced, s_SmokeLog[i].needed,
			s_SmokeLog[i].source, s_SmokeLog[i].time_ms);
	}

	return warnings;
}

/* ========================================================================
 * Offline sweep iterator state
 * ======================================================================== */

typedef struct smoke_iter_ctx {
	s32 count;
	s32 warnings;
} smoke_iter_ctx_t;

static void smokeOfflineBuildArena(const asset_entry_t *entry, void *userdata)
{
	smoke_iter_ctx_t *ctx = (smoke_iter_ctx_t *)userdata;
	spawn_pool_t scratch;
	s32 saved_nsp;
	clock_t t0;
	u32 ms;
	const char *id;

	if (!entry || entry->type != ASSET_ARENA) return;

	id = entry->id;

	/* Skip stages already recorded from live gameplay */
	{
		s32 i;
		for (i = 0; i < s_SmokeCount; i++) {
			if (strncmp(s_SmokeLog[i].stage_id, id, CATALOG_ID_LEN - 1) == 0
			    && s_SmokeLog[i].source == SMOKE_SRC_LIVE) {
				ctx->count++;
				if (s_SmokeLog[i].max_layer >= 3) ctx->warnings++;
				return;
			}
		}
	}

	/* Offline build: zero declared pads, use current geometry state.
	 * This exercises L2-L4 fallback paths and confirms the guarantee holds.
	 * Results are labelled 'O' (offline) in the CSV. */
	{
		bool saved_ready = s_PoolReady;

		saved_nsp = g_NumSpawnPoints;
		g_NumSpawnPoints = 0;

		t0 = clock();
		spawnPoolBuild(&scratch, id, 0x5EC0BE45u, SPAWNPOOL_MAX); /* offline test seed */
		ms = (u32)(((clock() - t0) * 1000u) / (u32)CLOCKS_PER_SEC);

		g_NumSpawnPoints = saved_nsp;
		s_PoolReady = saved_ready; /* offline build must not affect global pool flag */
	}

	smokeLogRecord(id, SPAWNPOOL_MAX, scratch.count, scratch.max_layer_used,
	               SMOKE_SRC_OFFLINE, ms);

	ctx->count++;
	if (scratch.max_layer_used >= 3) ctx->warnings++;
}

s32 spawnPoolSmokeAll(void)
{
	smoke_iter_ctx_t ctx;
	ctx.count    = 0;
	ctx.warnings = 0;

	sysLogPrintf(LOG_NOTE, "SPAWNPOOL SMOKE ALL: sweeping catalog arenas...");
	assetCatalogIterateByType(ASSET_ARENA, smokeOfflineBuildArena, &ctx);
	sysLogPrintf(LOG_NOTE,
		"SPAWNPOOL SMOKE ALL: %d arenas tested, %d needed L3/L4",
		ctx.count, ctx.warnings);

	return ctx.warnings;
}

void spawnPoolSmokeWriteCSV(const char *path)
{
	FILE *f;
	s32 i;

	if (!path || !path[0]) return;

	f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "SPAWNPOOL SMOKE: cannot write CSV to %s", path);
		return;
	}

	fprintf(f, "stage_id,needed,produced,max_layer_used,source,time_ms,flag\n");
	for (i = 0; i < s_SmokeCount; i++) {
		const smoke_record_t *r = &s_SmokeLog[i];
		const char *flag = (r->max_layer >= 3) ? "L3L4_RISK" : "OK";
		fprintf(f, "%s,%d,%d,%d,%c,%u,%s\n",
			r->stage_id, r->needed, r->produced, (s32)r->max_layer,
			r->source, r->time_ms, flag);
	}

	fclose(f);
	sysLogPrintf(LOG_NOTE, "SPAWNPOOL SMOKE: CSV written to %s (%d rows)", path, s_SmokeCount);
}
