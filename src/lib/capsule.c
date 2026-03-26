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
#include "lib/meshcollision.h"
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

	/* Try mesh-based collision first (if world mesh is built) */
	if (g_WorldMesh.ready && g_WorldMesh.numtris > 0) {
		f32 halfheight = (cast->ymax_offset - cast->ymin_offset) * 0.5f;
		struct coord meshNormal, meshHitPos;
		f32 meshFrac = meshSweepCapsuleWorld(&cast->start, &cast->move,
			cast->radius, halfheight, &meshNormal, &meshHitPos);

		if (meshFrac < 1.0f) {
			cast->hitfrac = meshFrac;
			cast->hitpos = meshHitPos;
			cast->hitnormal = meshNormal;
			cast->hitprop = NULL;
			cast->hitgeoflags = 0;

			/* Classify hit from normal */
			if (meshNormal.y > 0.7f) {
				cast->hittype = CAPSULE_HIT_FLOOR;
				cast->hitgeoflags = GEOFLAG_FLOOR1;
			} else if (meshNormal.y < -0.7f) {
				cast->hittype = CAPSULE_HIT_CEILING;
				cast->hitgeoflags = GEOFLAG_FLOOR2;
			} else {
				cast->hittype = CAPSULE_HIT_WALL;
				cast->hitgeoflags = GEOFLAG_WALL;
			}

			return meshFrac;
		}
	}

	/* Fall back to legacy stepped cdTestVolume sweep */

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

	/* Mesh-based floor detection (primary) */
	if (g_WorldMesh.ready && g_WorldMesh.numtris > 0) {
		f32 normalY = 1.0f;
		f32 meshFloor = meshFindFloor(pos, radius, &normalY);

		if (meshFloor > -29000.0f) {
			if (out_flags) *out_flags = GEOFLAG_FLOOR1;
			return meshFloor;
		}
	}

	/* Legacy fallback: cdFindGroundInfoAtCyl for BG + binary search for props */
	struct coord testpos;
	RoomNum testrooms[8];
	u16 floorcol = 0;
	u8 floortype = 0;
	u16 floorflags = 0;
	RoomNum floorroom = -1;
	s32 inlift = 0;
	struct prop *lift = NULL;

	testpos = *pos;
	roomsCopy(rooms, testrooms);
	f32 bgGround = cdFindGroundInfoAtCyl(&testpos, radius, testrooms,
			&floorcol, &floortype, &floorflags, &floorroom, &inlift, &lift);

	if (out_flags) *out_flags = floorflags;
	return bgGround;
}

f32 capsuleFindCeiling(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop)
{
	if (out_prop) *out_prop = NULL;

	/* Mesh-based ceiling detection (primary) */
	if (g_WorldMesh.ready && g_WorldMesh.numtris > 0) {
		f32 meshCeil = meshFindCeiling(pos, radius);
		if (meshCeil < 90000.0f) {
			return meshCeil;
		}
	}

	/* Legacy fallback */
	f32 bgCeiling = 99999.0f;
	struct coord testpos = *pos;
	cdFindCeilingRoomYColourFlagsAtPos(&testpos, rooms, &bgCeiling, NULL, NULL);
	return bgCeiling;
}
