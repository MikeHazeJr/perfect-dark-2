#ifndef _IN_LIB_CAPSULE_H
#define _IN_LIB_CAPSULE_H

/* Set to 1 to enable the custom swept-capsule collision system.
 * Set to 0 to fall back to the original N64 collision behaviour.
 *
 * Status: ENABLED (Track G, c038, 2026-05-18). Stage 1 keeps the legacy
 * cdTestVolume broadphase authoritative. Stage 2 samples the rendered BG
 * and prop model triangles around the full capsule skin so unflagged
 * sloped ceilings, table tops, and half walls can still block movement. */
#define PC_CAPSULE_ENABLED 1

#include <ultra64.h>
#include "data.h"
#include "types.h"

/**
 * PC: Swept capsule collision system
 *
 * Replaces the N64-era hacky workarounds (prop surface binary search, simple
 * bounding box ceiling detection, etc.) with a proper swept capsule cast that
 * projects the player's collision volume along a movement vector BEFORE
 * translating, finds the precise first contact point and source object, and
 * returns detailed collision information.
 *
 * The capsule is defined by two sphere centres (bottom/top of the cylinder
 * body) and a radius. For the player this maps to:
 *   bottom = manground + radius   (feet + radius gives bottom sphere centre)
 *   top    = manground + headheight - radius  (head - radius gives top sphere centre)
 *   radius = bond2.radius (~30 units)
 *
 * The sweep uses sub-step sampling along the movement vector, testing the
 * existing geometry collection system at each step. On modern x86_64 this is
 * trivially fast even with 16-32 sub-steps.
 */

/* Result of a swept capsule cast */
#define CAPSULE_HIT_NONE    0
#define CAPSULE_HIT_FLOOR   1
#define CAPSULE_HIT_CEILING 2
#define CAPSULE_HIT_WALL    3
#define CAPSULE_HIT_PROP    4

#define CAPSULE_NORMAL_FLOOR_MIN_Y    0.35f
#define CAPSULE_NORMAL_CEILING_MAX_Y -0.35f

static inline void capsuleOrientNormalAgainstMove(struct coord *normal,
		const struct coord *move)
{
	if (normal && move) {
		f32 dot = normal->x * move->x + normal->y * move->y + normal->z * move->z;
		if (dot > 0.0f) {
			normal->x = -normal->x;
			normal->y = -normal->y;
			normal->z = -normal->z;
		}
	}
}

static inline s32 capsuleClassifyNormal(const struct coord *normal)
{
	if (!normal) {
		return CAPSULE_HIT_WALL;
	}
	if (normal->y >= CAPSULE_NORMAL_FLOOR_MIN_Y) {
		return CAPSULE_HIT_FLOOR;
	}
	if (normal->y <= CAPSULE_NORMAL_CEILING_MAX_Y) {
		return CAPSULE_HIT_CEILING;
	}
	return CAPSULE_HIT_WALL;
}

struct capsulecast {
	/* Input: capsule definition */
	struct coord start;       /* starting position (player eye/prop pos) */
	f32 radius;               /* capsule radius */
	f32 ymin_offset;          /* offset from start.y to capsule bottom (negative) */
	f32 ymax_offset;          /* offset from start.y to capsule top (positive) */

	/* Input: movement vector */
	struct coord move;        /* desired movement delta */

	/* Input: world context */
	RoomNum rooms[8];         /* rooms the player is in (copied) */
	u32 cdtypes;              /* collision types to test (CDTYPE_*) */
	struct prop *selfprop;    /* moving prop; pass explicitly for movement */

	/* Output: result */
	s32 hittype;              /* CAPSULE_HIT_* */
	f32 hitfrac;              /* fraction along move where first hit occurs [0..1] */
	struct coord hitpos;      /* world position of contact */
	struct coord hitnormal;   /* surface normal at contact (approximate) */
	struct prop *hitprop;     /* prop that was hit, or NULL for BG */
	u16 hitgeoflags;          /* geo flags of the surface hit */
	u8 hitfromrendered;       /* 1 if Stage 2 rendered triangles supplied the hit */
};

/**
 * Perform a swept capsule cast along the movement vector.
 *
 * Projects the capsule from `start` along `move`, testing collisions at
 * sub-step intervals. Returns the fraction [0..1] of the movement that is
 * safe (no collision). A return value of 1.0 means the full movement is clear.
 *
 * @param cast  Filled in with hit information on return.
 * @return      The safe fraction of movement [0..1].
 */
f32 capsuleSweep(struct capsulecast *cast);

/**
 * Find the floor height below the capsule's current position using a
 * downward capsule cast.
 *
 * @param pos       Player position (eye level)
 * @param radius    Capsule radius
 * @param ymin_off  Offset from pos.y to capsule bottom
 * @param ymax_off  Offset from pos.y to capsule top
 * @param rooms     Room list
 * @param cdtypes   Collision types
 * @param out_prop  If non-NULL, receives the prop standing on (or NULL for BG)
 * @param out_flags If non-NULL, receives the geo flags of the floor surface
 * @return          The Y coordinate of the floor, or -30000 if none found
 */
f32 capsuleFindFloor(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                     RoomNum *rooms, u32 cdtypes,
                     struct prop **out_prop, u16 *out_flags);

f32 capsuleFindFloorForProp(struct prop *selfprop, struct coord *pos,
                     f32 radius, f32 ymin_off, f32 ymax_off,
                     RoomNum *rooms, u32 cdtypes,
                     struct prop **out_prop, u16 *out_flags);

/**
 * Stage 2 rendered-triangle ray cast (c038, two-stage capsule sweep).
 *
 * Fires a single ray from `from` to `to` through the room list, walking
 * `g_Rooms[].vtxbatches[]` per room via bgTestHitInRoom. Returns the
 * normalized fraction along the ray to the closest hit, or 1.0 for clear.
 *
 * Catches rendered geometry that lacks the GEOFLAG_WALL/FLOOR colliders
 * (sloped ceilings, table tops, half walls). Pairs with the Stage 1
 * cdTestVolume path: callers run Stage 1 first (cheap pre-cull) and then
 * use this as the authoritative validator. Mike directive 2026-05-17:
 * "should be able to jump on top without falling through the geometry".
 *
 * @param from        Ray start in world space
 * @param to          Ray end in world space
 * @param rooms       Room list (8-slot array, terminator <0)
 * @param out_normal  Optional; receives per-triangle face normal on hit
 * @return            Fraction [0..1] of the ray to the closest hit; 1.0 if no hit
 */
f32 capsuleStage2RayCast(const struct coord *from, const struct coord *to,
                         const RoomNum *rooms, struct coord *out_normal);

/**
 * Stage 2 floor probe: rendered-triangle downward probe to catch table
 * tops / half walls / other rendered-only floors not seen by
 * cdFindGroundInfoAtCyl (which only reads GEOFLAG_FLOOR-flagged tiles).
 *
 * Casts straight down from `pos` by `maxdepth` units and returns the Y
 * of the closest rendered-triangle hit, or -30000.0f if no hit.
 *
 * @param pos       Probe origin (world space; typically the player's foot pos)
 * @param rooms     Room list (8-slot array, terminator <0)
 * @param maxdepth  Maximum probe distance below pos.y
 * @return          World-space Y of the closest hit, or -30000.0f if no hit
 */
f32 capsuleStage2FloorProbe(const struct coord *pos, const RoomNum *rooms,
                            f32 maxdepth);

f32 capsuleFindRenderedFloor(struct prop *selfprop, const struct coord *pos,
                     f32 radius, f32 ymin_off, const RoomNum *rooms,
                     f32 maxdepth, struct coord *out_normal);

f32 capsuleFindRenderedCeiling(struct prop *selfprop, const struct coord *pos,
                     f32 radius, f32 ymax_off, const RoomNum *rooms,
                     f32 maxheight, struct coord *out_normal);

/**
 * Find the ceiling height above the capsule's current position using an
 * upward capsule cast.
 *
 * @param pos       Player position (eye level)
 * @param radius    Capsule radius
 * @param ymin_off  Offset from pos.y to capsule bottom
 * @param ymax_off  Offset from pos.y to capsule top
 * @param rooms     Room list
 * @param cdtypes   Collision types
 * @param out_prop  If non-NULL, receives the prop hit (or NULL for BG)
 * @return          The Y coordinate of the ceiling, or 99999 if none found
 */
f32 capsuleFindCeiling(struct coord *pos, f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop);

f32 capsuleFindCeilingForProp(struct prop *selfprop, struct coord *pos,
                       f32 radius, f32 ymin_off, f32 ymax_off,
                       RoomNum *rooms, u32 cdtypes,
                       struct prop **out_prop);

#endif
