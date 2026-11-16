/**
 * meshraygrid.h -- ray casts against a mesh grid (B-1189)
 *
 * Dependency-free so pd-tests can check the grid walk against a brute-force
 * cast. meshcollision.c owns the world grid and the per-call stamps; these
 * functions only read the grid they are given.
 */

#ifndef MESHRAYGRID_H
#define MESHRAYGRID_H

#include "lib/meshcollision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Two-sided ray (segment from -> to) vs triangle. On a hit, *out_frac is the
 * fraction along the segment, in [0, 1]. */
bool meshRayVsTri(const struct coord *from, const struct coord *to,
		const struct meshtri *tri, f32 *out_frac);

/* Nearest hit of the segment against the grid's triangles whose flags
 * intersect mask (mask 0: every triangle). Walks only the XZ cells the
 * segment crosses, in order, and stops once no later cell can hold a nearer
 * hit. stamps holds one entry per grid triangle; an entry equal to stamp
 * means "already tested in this call", so the caller passes a value it has
 * not used before. Returns the hit fraction, or 1.0 when clear (*out_tri
 * is then -1). out_normal receives the hit triangle's face normal. */
f32 meshGridRayCast(const struct meshgrid *grid, const struct coord *from,
		const struct coord *to, u16 mask, u32 *stamps, u32 stamp,
		struct coord *out_normal, s32 *out_tri);

/* The same query over every triangle, for tests. */
f32 meshTrisRayCast(const struct meshtri *tris, s32 numtris,
		const struct coord *from, const struct coord *to, u16 mask,
		struct coord *out_normal, s32 *out_tri);

#ifdef __cplusplus
}
#endif

#endif
