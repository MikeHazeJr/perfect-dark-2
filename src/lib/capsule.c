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
			return safefrac;
		}
	}

	propSetPerimEnabled(g_Vars.currentplayer->prop, true);
	return 1.0f;
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
		return propFloor;
	}

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

	return (propCeiling < bgCeiling) ? propCeiling : bgCeiling;
}
