#ifndef _IN_LIB_CAPSULE_H
#define _IN_LIB_CAPSULE_H

/* Set to 1 to enable the custom swept-capsule collision system.
 * Currently disabled — the system is not yet integrated into the movement
 * pipeline. Set to 0 to fall back to the original N64 collision behaviour. */
#define PC_CAPSULE_ENABLED 0

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

	/* Output: result */
	s32 hittype;              /* CAPSULE_HIT_* */
	f32 hitfrac;              /* fraction along move where first hit occurs [0..1] */
	struct coord hitpos;      /* world position of contact */
	struct coord hitnormal;   /* surface normal at contact (approximate) */
	struct prop *hitprop;     /* prop that was hit, or NULL for BG */
	u16 hitgeoflags;          /* geo flags of the surface hit */
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

#endif
