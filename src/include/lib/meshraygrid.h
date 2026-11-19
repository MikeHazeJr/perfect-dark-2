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

/* Bounded static clearance for a flat, collision-authored walking profile.
 * Endpoints require support in their specified rooms. Floor rails inside the
 * footprint may use any positive room in the caller's validated room domain.
 * The caller bounds the mesh work and supplies actor dimensions. Equal
 * endpoints check a stationary footprint. The center segment may be up to
 * 8,000 native units long. No resident stage or prop state is queried. */
s32 meshCheckFlatWalk(const struct colmesh *mesh, const struct coord *from, RoomNum from_room,
		const struct coord *to, RoomNum to_room, f32 radius, f32 height);

#ifdef __cplusplus
}
#endif

#endif
