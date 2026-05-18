/**
 * PC: Swept capsule collision system
 *
 * Modern replacement for N64-era collision workarounds. Projects a capsule
 * along a movement path before translating, finding precise collision points.
 *
 * Uses the existing geometry collection and testing infrastructure (cdTestVolume,
 * cdFindGroundInfoAtCyl, cdFindCeilingRoomYColourFlagsAtPos) but samples at
 * sub-step intervals along the movement path. This is a "stepped sweep" —
 * trivially fast on modern x86_64 even with many sub-steps.
 */

#include <ultra64.h>
#include "constants.h"
#include "game/bg.h"
#include "game/prop.h"
#include "game/bondmove.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/capsule.h"
#include "data.h"
#include "types.h"
#include "system.h"

/* CAPSULE: log markers (Track G prep, c038).
 *
 * Canonical instrumentation for the physics-collision pillar.  Each marker
 * uses the "CAPSULE:" prefix so smoke-verify assertions can grep a single
 * stable token and so the log channel filter routes them with the rest of
 * the engine logs (no dedicated channel -- prefix is unfiltered, falls
 * through the default LOG_CH_GAME pass).
 *
 * Gated on PD_DEV_BUILD: dev / prerelease / local builds get the lines,
 * stable release builds compile them out entirely (no printf cost).
 * capsule.c links into pd only (never pd-server), so the gate is sound. */
#if defined(PD_DEV_BUILD)
#define CAPSULE_LOG(...) sysLogPrintf(LOG_NOTE, "CAPSULE: " __VA_ARGS__)
#else
#define CAPSULE_LOG(...) ((void)0)
#endif

/* Number of sub-steps for the sweep. 16 gives ~2-unit resolution for typical
 * jump moves of ~30 units. On modern hardware this is trivially cheap. */
#define CAPSULE_SWEEP_STEPS 16

/* Maximum probe distance for floor/ceiling searches (units) */
#define CAPSULE_FLOOR_PROBE   2000.0f
#define CAPSULE_CEILING_PROBE 2000.0f

/* Sub-step size for floor/ceiling binary search */
#define CAPSULE_BSEARCH_ITERS 12

f32 capsuleSweep(struct capsulecast *cast)
{
	f32 stepfrac = 1.0f / (f32)CAPSULE_SWEEP_STEPS;
	struct coord testpos;
	RoomNum testrooms[8];
	s32 i;

	cast->hittype = CAPSULE_HIT_NONE;
	cast->hitfrac = 1.0f;
	cast->hitprop = NULL;
	cast->hitgeoflags = 0;

	/* Trivial case: no movement */
	f32 movelen2 = cast->move.x * cast->move.x
	             + cast->move.y * cast->move.y
	             + cast->move.z * cast->move.z;
	if (movelen2 < 0.001f) {
		return 1.0f;
	}

	CAPSULE_LOG("sweep enter start=(%.1f,%.1f,%.1f) move=(%.1f,%.1f,%.1f) radius=%.1f ymin=%.1f ymax=%.1f",
			cast->start.x, cast->start.y, cast->start.z,
			cast->move.x, cast->move.y, cast->move.z,
			cast->radius, cast->ymin_offset, cast->ymax_offset);

	/* Disable own perim so we don't collide with ourselves */
	propSetPerimEnabled(g_Vars.currentplayer->prop, false);

	for (i = 1; i <= CAPSULE_SWEEP_STEPS; i++) {
		f32 frac = stepfrac * (f32)i;

		testpos.x = cast->start.x + cast->move.x * frac;
		testpos.y = cast->start.y + cast->move.y * frac;
		testpos.z = cast->start.z + cast->move.z * frac;

		/* Determine rooms at test position */
		roomsCopy(cast->rooms, testrooms);
		func0f065e74(&cast->start, cast->rooms, &testpos, testrooms);
		bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, testrooms);

		/* Test the capsule volume at this position */
		s32 result = cdTestVolume(&testpos, cast->radius, testrooms,
				cast->cdtypes, CHECKVERTICAL_YES,
				cast->ymax_offset, cast->ymin_offset);

		if (result == CDRESULT_COLLISION) {
			/* Found a collision — the safe fraction is the previous step */
			f32 safefrac = stepfrac * (f32)(i - 1);

			cast->hitfrac = safefrac;

			/* Compute approximate hit position */
			cast->hitpos.x = cast->start.x + cast->move.x * frac;
			cast->hitpos.y = cast->start.y + cast->move.y * frac;
			cast->hitpos.z = cast->start.z + cast->move.z * frac;

			/* Identify what we hit */
			struct prop *obstacle = cdGetObstacleProp();
			cast->hitprop = obstacle;
			cast->hitgeoflags = cdGetGeoFlags();

			/* Classify the hit based on movement direction and geo flags */
			if (cast->move.y > 0.1f && (cast->hitgeoflags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))) {
				cast->hittype = CAPSULE_HIT_CEILING;
			} else if (cast->move.y < -0.1f && (cast->hitgeoflags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))) {
				cast->hittype = CAPSULE_HIT_FLOOR;
			} else if (cast->hitgeoflags & GEOFLAG_WALL) {
				cast->hittype = CAPSULE_HIT_WALL;
			} else if (obstacle) {
				cast->hittype = CAPSULE_HIT_PROP;
			} else {
				/* Fallback classification by movement direction */
				if (cast->move.y > 0.1f) {
					cast->hittype = CAPSULE_HIT_CEILING;
				} else if (cast->move.y < -0.1f) {
					cast->hittype = CAPSULE_HIT_FLOOR;
				} else {
					cast->hittype = CAPSULE_HIT_WALL;
				}
			}

			/* Approximate surface normal from movement direction */
			f32 invlen = 1.0f;
			if (movelen2 > 0.001f) {
				/* We don't need a precise normal — just the negated, normalized
				 * movement direction as an approximation. The existing system
				 * doesn't give us surface normals from cdTestVolume anyway. */
				f32 len = sqrtf(movelen2);
				invlen = 1.0f / len;
			}
			cast->hitnormal.x = -cast->move.x * invlen;
			cast->hitnormal.y = -cast->move.y * invlen;
			cast->hitnormal.z = -cast->move.z * invlen;

			propSetPerimEnabled(g_Vars.currentplayer->prop, true);

#if defined(PD_DEV_BUILD)
			{
				const char *hitclass;
				switch (cast->hittype) {
				case CAPSULE_HIT_FLOOR:   hitclass = "FLOOR";   break;
				case CAPSULE_HIT_CEILING: hitclass = "CEILING"; break;
				case CAPSULE_HIT_WALL:    hitclass = "WALL";    break;
				case CAPSULE_HIT_PROP:    hitclass = "PROP";    break;
				default:                  hitclass = "NONE";    break;
				}
				CAPSULE_LOG("sweep result=BLOCKED type=%s dist=%.3f geoflags=0x%04x step=%d/%d",
						hitclass, safefrac, cast->hitgeoflags, i, CAPSULE_SWEEP_STEPS);
			}
#endif

			return safefrac;
		}
	}

	propSetPerimEnabled(g_Vars.currentplayer->prop, true);

	/* Stage 2 (c038, Mike directive 2026-05-17): even if Stage 1 said
	 * CLEAR, a sloped ceiling or other rendered-only triangle may block
	 * the movement. Cast a rendered-triangle ray from start to start+move
	 * and use that as the authoritative hit if it lands before 1.0.
	 * Net effect: full coverage of GEOFLAG_WALL/FLOOR + rendered triangles
	 * with no matching collider. */
	{
		struct coord end;
		end.x = cast->start.x + cast->move.x;
		end.y = cast->start.y + cast->move.y;
		end.z = cast->start.z + cast->move.z;
		struct coord normal = {0.0f, 0.0f, 0.0f};
		f32 stage2_frac = capsuleStage2RayCast(&cast->start, &end, cast->rooms, &normal);
		if (stage2_frac < 1.0f) {
			cast->hitfrac = stage2_frac;
			cast->hitpos.x = cast->start.x + cast->move.x * stage2_frac;
			cast->hitpos.y = cast->start.y + cast->move.y * stage2_frac;
			cast->hitpos.z = cast->start.z + cast->move.z * stage2_frac;
			cast->hitnormal = normal;
			cast->hitprop = NULL;
			cast->hitgeoflags = 0;
			/* Classify by vertical move direction. Stage 2 hits are
			 * always rendered BG geometry; we don't have prop/chr info
			 * here. */
			if (cast->move.y > 0.1f) {
				cast->hittype = CAPSULE_HIT_CEILING;
			} else if (cast->move.y < -0.1f) {
				cast->hittype = CAPSULE_HIT_FLOOR;
			} else {
				cast->hittype = CAPSULE_HIT_WALL;
			}
			CAPSULE_LOG("sweep result=BLOCKED_STAGE2 dist=%.3f hittype=%d (Stage 1 clear)",
				stage2_frac, cast->hittype);
			return stage2_frac;
		}
	}

	CAPSULE_LOG("sweep result=CLEAR dist=1.000");
	return 1.0f;
}

/* ============================================================
 * Stage 2 (c038): rendered-triangle ray cast + floor probe.
 *
 * Bridges the gap where Stage 1 (cdTestVolume) misses geometry that
 * is rendered but not flagged as a collider. Two common cases reported
 * by Mike (2026-05-17):
 *   - Sloped ceilings: jumping straight up passes through the sloped
 *     ceiling triangle because no GEOFLAG_WALL tile exists.
 *   - Table tops / half walls: standing on them, the player falls
 *     through because cdFindGroundInfoAtCyl only finds GEOFLAG_FLOOR
 *     tiles and these surfaces are just rendered display-list triangles.
 *
 * Both fixes hinge on bgTestHitInRoom (src/game/bg.c:4471), which walks
 * the same rendered vtxbatches the Laptop Gun sticky path uses. We do a
 * single ray per call here; the design doc proposed up to 15 rays
 * (capsule center + skin samples) but the single ray is sufficient for
 * the c038 wall-jump / table-top class because the test ray is the
 * capsule's central axis and the radius is small (~30u). We can extend
 * to lateral samples later if a regression test demands it.
 * ============================================================ */

f32 capsuleStage2RayCast(const struct coord *from, const struct coord *to,
                         const RoomNum *rooms, struct coord *out_normal)
{
	if (!from || !to || !rooms) {
		return 1.0f;
	}

	f32 mx = to->x - from->x;
	f32 my = to->y - from->y;
	f32 mz = to->z - from->z;
	f32 movelen2 = mx * mx + my * my + mz * mz;
	if (movelen2 < 0.001f) {
		return 1.0f;
	}

	f32 best_frac = 1.0f;
	f32 best_frac2 = movelen2; /* squared distance from `from` to best hit */
	s32 i;

	for (i = 0; i < 8; i++) {
		s32 roomnum = (s32)rooms[i];
		if (roomnum < 0) break;
		if (roomnum == 0) continue; /* room 0 is sentinel "no room" */

		struct coord f = *from;
		struct coord t = *to;
		struct hitthing hit;
		if (bgTestHitInRoom(&f, &t, roomnum, &hit)) {
			f32 dx = hit.pos.x - from->x;
			f32 dy = hit.pos.y - from->y;
			f32 dz = hit.pos.z - from->z;
			f32 hit_len2 = dx * dx + dy * dy + dz * dz;
			if (hit_len2 < best_frac2) {
				best_frac2 = hit_len2;
				/* Normalize frac by movement length (geometric, sqrt-free
				 * comparison above; sqrt only at the end). */
				best_frac = (movelen2 > 0.0001f)
					? sqrtf(hit_len2 / movelen2)
					: 0.0f;
				if (out_normal) {
					*out_normal = hit.unk0c;
				}
			}
		}
	}

	if (best_frac < 1.0f) {
		CAPSULE_LOG("stage2 raycast hit frac=%.3f from=(%.1f,%.1f,%.1f) to=(%.1f,%.1f,%.1f)",
			best_frac, from->x, from->y, from->z, to->x, to->y, to->z);
	}

	return best_frac;
}

f32 capsuleStage2FloorProbe(const struct coord *pos, const RoomNum *rooms,
                            f32 maxdepth)
{
	if (!pos || !rooms || maxdepth <= 0.0f) {
		return -30000.0f;
	}

	struct coord from = *pos;
	struct coord to;
	to.x = pos->x;
	to.y = pos->y - maxdepth;
	to.z = pos->z;

	struct coord normal = {0.0f, 0.0f, 0.0f};
	f32 frac = capsuleStage2RayCast(&from, &to, rooms, &normal);
	if (frac >= 1.0f) {
		return -30000.0f;
	}

	f32 hit_y = pos->y - maxdepth * frac;
	CAPSULE_LOG("stage2 floor probe pos=(%.1f,%.1f,%.1f) hit_y=%.1f normal=(%.2f,%.2f,%.2f)",
		pos->x, pos->y, pos->z, hit_y, normal.x, normal.y, normal.z);
	return hit_y;
}

f32 capsuleFindFloor(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                     RoomNum *rooms, u32 cdtypes,
                     struct prop **out_prop, u16 *out_flags)
{
	if (out_prop) *out_prop = NULL;
	if (out_flags) *out_flags = 0;

	/* Legacy floor detection */
	struct coord testpos;
	RoomNum testrooms[8];
	f32 bgGround;
	f32 propFloor = -30000.0f;
	u16 floorcol = 0;
	u8 floortype = 0;
	u16 floorflags = 0;
	RoomNum floorroom = -1;
	s32 inlift = 0;
	struct prop *lift = NULL;

	testpos = *pos;
	roomsCopy(rooms, testrooms);
	bgGround = cdFindGroundInfoAtCyl(&testpos, radius, testrooms,
			&floorcol, &floortype, &floorflags, &floorroom, &inlift, &lift);

	if (out_flags) *out_flags = floorflags;

	/* Probe downward with capsule volume to find prop surfaces */
	f32 feetY = pos->y + ymin_off;

	if (feetY > bgGround + 2.0f) {
		f32 top = feetY;
		f32 bot = bgGround;

		propSetPerimEnabled(g_Vars.currentplayer->prop, false);

		f32 mid = (top + bot) * 0.5f;
		testpos.x = pos->x;
		testpos.y = mid - ymin_off;
		testpos.z = pos->z;
		roomsCopy(rooms, testrooms);
		bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, testrooms);

		s32 midResult = cdTestVolume(&testpos, radius, testrooms,
				cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

		if (midResult == CDRESULT_COLLISION) {
			for (s32 iter = 0; iter < CAPSULE_BSEARCH_ITERS; iter++) {
				f32 test = (top + bot) * 0.5f;
				testpos.y = test - ymin_off;
				roomsCopy(rooms, testrooms);
				bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, testrooms);

				s32 r = cdTestVolume(&testpos, radius, testrooms,
						cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

				if (r == CDRESULT_COLLISION) {
					bot = test;
				} else {
					top = test;
				}
			}

			propFloor = top;

			if (out_prop) {
				*out_prop = cdGetObstacleProp();
			}
		}

		propSetPerimEnabled(g_Vars.currentplayer->prop, true);
	}

	if (propFloor > bgGround + 1.0f) {
		CAPSULE_LOG("findFloor result=PROP y=%.2f bgGround=%.2f flags=0x%04x",
				propFloor, bgGround, floorflags);
		return propFloor;
	}

	CAPSULE_LOG("findFloor result=BG y=%.2f flags=0x%04x", bgGround, floorflags);
	return bgGround;
}

f32 capsuleFindCeiling(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop)
{
	if (out_prop) *out_prop = NULL;

	/* Legacy ceiling detection */
	f32 bgCeiling = 99999.0f;
	f32 propCeiling = 99999.0f;
	struct coord testpos;
	RoomNum testrooms[8];

	testpos = *pos;
	cdFindCeilingRoomYColourFlagsAtPos(&testpos, rooms, &bgCeiling, NULL, NULL);

	/* Probe upward with capsule volume for prop/wall ceilings */
	f32 headY = pos->y + ymax_off;
	f32 maxProbe = headY + CAPSULE_CEILING_PROBE;

	if (maxProbe > headY + 10.0f) {
		f32 bot = headY;
		f32 top = (bgCeiling < maxProbe) ? bgCeiling : maxProbe;

		if (top > bot + 5.0f) {
			propSetPerimEnabled(g_Vars.currentplayer->prop, false);

			f32 probeY = bot + 10.0f;
			testpos.x = pos->x;
			testpos.y = probeY - ymax_off;
			testpos.z = pos->z;
			roomsCopy(rooms, testrooms);
			bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, testrooms);

			s32 probeResult = cdTestVolume(&testpos, radius, testrooms,
					cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

			if (probeResult == CDRESULT_COLLISION) {
				propCeiling = headY;
				if (out_prop) *out_prop = cdGetObstacleProp();
			} else {
				for (s32 iter = 0; iter < CAPSULE_BSEARCH_ITERS; iter++) {
					f32 test = (bot + top) * 0.5f;
					testpos.y = test - ymax_off;
					roomsCopy(rooms, testrooms);
					bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, testrooms);

					s32 r = cdTestVolume(&testpos, radius, testrooms,
							cdtypes, CHECKVERTICAL_YES, ymax_off, ymin_off);

					if (r == CDRESULT_COLLISION) {
						top = test;
					} else {
						bot = test;
					}
				}

				if (top < bgCeiling - 1.0f) {
					propCeiling = top;
					if (out_prop) *out_prop = cdGetObstacleProp();
				}
			}

			propSetPerimEnabled(g_Vars.currentplayer->prop, true);
		}
	}

	{
		f32 result = (propCeiling < bgCeiling) ? propCeiling : bgCeiling;
		CAPSULE_LOG("findCeiling result=%s y=%.2f bgCeiling=%.2f propCeiling=%.2f",
				(propCeiling < bgCeiling) ? "PROP" : "BG",
				result, bgCeiling, propCeiling);
		return result;
	}
}
