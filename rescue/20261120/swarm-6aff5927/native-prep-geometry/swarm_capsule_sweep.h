#ifndef SWARM_CAPSULE_SWEEP_H
#define SWARM_CAPSULE_SWEEP_H

#include "lib/meshcollision.h"

#ifdef __cplusplus
extern "C" {
#endif

struct swarm_capsule {
	struct coord center;
	struct coord axis;
	f32 radius;
	f32 half_segment; /* Half the internal spine length, excluding the spherical ends. */
};

enum swarm_sweep_result {
	SWARM_SWEEP_INVALID,
	SWARM_SWEEP_CLEAR,
	SWARM_SWEEP_HIT,
	SWARM_SWEEP_START_OVERLAP,
	SWARM_SWEEP_ITERATION_LIMIT,
	SWARM_SWEEP_CANDIDATE_LIMIT,
};

struct swarm_sweep_contact {
	f32 fraction;
	struct coord point;
	struct coord normal;
	s32 triangle;
	RoomNum room;
	u16 flags;
};

/* Sweeps a fixed-axis capsule along a translation through world-space native
 * triangles. A zero mask tests all triangles. Axis length is normalized and a
 * sphere may use a zero axis. Clear returns fraction 1. Hit returns the safe
 * fraction and an outward contact normal. An initial overlap, invalid input
 * or candidate limit returns 0. Iteration exhaustion returns the last safe
 * fraction, but the contact does not qualify as a support witness. */
enum swarm_sweep_result swarmCapsuleSweepTriangles(const struct swarm_capsule *capsule,
		const struct coord *translation, const struct meshtri *triangles,
		s32 count, u16 mask, s32 candidate_limit, struct swarm_sweep_contact *contact);

/* Static-world variant using the existing XZ grid. Stamps belong to the caller
 * and need one element per triangle and a fresh nonzero mark for each query.
 * Reinitialize stamps before reusing a mark, including on stage/grid change. */
enum swarm_sweep_result swarmCapsuleSweepGrid(const struct swarm_capsule *capsule,
		const struct coord *translation, const struct meshgrid *grid,
		u16 mask, s32 candidate_limit, u32 *stamps, s32 stamp_count,
		u32 mark, struct swarm_sweep_contact *contact);

#ifdef __cplusplus
}
#endif
#endif
