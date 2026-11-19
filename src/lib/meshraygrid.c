/**
 * meshraygrid.c -- ray casts against a mesh grid (B-1189)
 *
 * The background hit tests of stages built from a scenario source (shots,
 * sticky projectiles, blood splats, spawn clearance) cast rays against the
 * static world collision mesh. Bullet rays run up to 65536 units and the
 * spawn pool casts 18 rays per candidate, so this walks only the grid cells
 * a ray crosses instead of testing every triangle.
 *
 * Kept free of game state so pd-tests can compare the walk with a
 * brute-force cast. This file is C11 game code (not C++).
 */

#include <float.h>
#include <math.h>

#include "lib/meshraygrid.h"

/* The grid keeps floor-to-cell conversion local: the game supplies its own
 * floorf (src/game/floor.c), which pd-tests does not link. */
static f32 meshAbs(f32 v)
{
	return v < 0.0f ? -v : v;
}

static s32 meshFloorIndex(f32 v)
{
	s32 i = (s32)v;
	return v < (f32)i ? i - 1 : i;
}

/* Moller-Trumbore, two-sided, with the tolerances of meshcollision.c's
 * meshRayVsTriangle so both agree on what a hit is. */
bool meshRayVsTri(const struct coord *from, const struct coord *to,
		const struct meshtri *tri, f32 *out_frac)
{
	f32 dirx = to->x - from->x;
	f32 diry = to->y - from->y;
	f32 dirz = to->z - from->z;
	f32 e1x = tri->v1.x - tri->v0.x;
	f32 e1y = tri->v1.y - tri->v0.y;
	f32 e1z = tri->v1.z - tri->v0.z;
	f32 e2x = tri->v2.x - tri->v0.x;
	f32 e2y = tri->v2.y - tri->v0.y;
	f32 e2z = tri->v2.z - tri->v0.z;
	f32 px = diry * e2z - dirz * e2y;
	f32 py = dirz * e2x - dirx * e2z;
	f32 pz = dirx * e2y - diry * e2x;
	f32 det = e1x * px + e1y * py + e1z * pz;
	f32 invdet;
	f32 tx;
	f32 ty;
	f32 tz;
	f32 qx;
	f32 qy;
	f32 qz;
	f32 u;
	f32 v;
	f32 t;

	if (meshAbs(det) < 0.0001f) {
		return false;
	}

	invdet = 1.0f / det;
	tx = from->x - tri->v0.x;
	ty = from->y - tri->v0.y;
	tz = from->z - tri->v0.z;

	u = (tx * px + ty * py + tz * pz) * invdet;
	if (u < -0.001f || u > 1.001f) {
		return false;
	}

	qx = ty * e1z - tz * e1y;
	qy = tz * e1x - tx * e1z;
	qz = tx * e1y - ty * e1x;

	v = (dirx * qx + diry * qy + dirz * qz) * invdet;
	if (v < -0.001f || u + v > 1.001f) {
		return false;
	}

	t = (e2x * qx + e2y * qy + e2z * qz) * invdet;
	if (t < 0.0f || t > 1.0f) {
		return false;
	}

	if (out_frac) {
		*out_frac = t;
	}
	return true;
}

static void meshGridTestCell(const struct meshgrid *grid, s32 cellindex,
		const struct coord *from, const struct coord *to, u16 mask,
		u32 *stamps, u32 stamp, f32 *best, s32 *besttri)
{
	const struct meshgridcell *cell = &grid->cells[cellindex];
	s32 i;

	for (i = 0; i < cell->count; i++) {
		const s32 ti = cell->triindices[i];
		const struct meshtri *tri;
		f32 frac;

		if (ti < 0 || ti >= grid->numtris || stamps[ti] == stamp) {
			continue;
		}

		stamps[ti] = stamp;
		tri = &grid->tris[ti];

		if (mask != 0 && (tri->flags & mask) == 0) {
			continue;
		}

		if (meshRayVsTri(from, to, tri, &frac) && frac < *best) {
			*best = frac;
			*besttri = ti;
		}
	}
}

f32 meshGridRayCast(const struct meshgrid *grid, const struct coord *from,
		const struct coord *to, u16 mask, u32 *stamps, u32 stamp,
		struct coord *out_normal, s32 *out_tri)
{
	const f32 size = MESHGRID_CELL_SIZE;
	f32 best = 1.0f;
	s32 besttri = -1;
	f32 fx;
	f32 fz;
	f32 gdx;
	f32 gdz;
	s32 cx;
	s32 cz;
	s32 stepx;
	s32 stepz;
	f32 tmaxx;
	f32 tmaxz;
	f32 tdeltax;
	f32 tdeltaz;
	s32 steps;

	if (out_tri) {
		*out_tri = -1;
	}

	if (grid == NULL || from == NULL || to == NULL || stamps == NULL
			|| grid->tris == NULL || grid->cells == NULL || grid->numtris <= 0
			|| grid->cellsx <= 0 || grid->cellsz <= 0) {
		return 1.0f;
	}

	/* A grid capped at MESHGRID_MAX_DIM cells (a stage wider than 32768
	 * units, such as Crash Site) lists every triangle past the cap in its
	 * last row or column: meshWorldFinalize clamps both ends of a
	 * triangle's cell range (meshGridCell, B-1210), and the walk below
	 * clamps cells past the last index to it. So capped grids are walked
	 * too, with no pass over every triangle. */

	/* Grid space: one unit per cell, origin at the mesh minimum. */
	fx = (from->x - grid->originx) / size;
	fz = (from->z - grid->originz) / size;
	gdx = (to->x - from->x) / size;
	gdz = (to->z - from->z) / size;

	cx = meshFloorIndex(fx);
	cz = meshFloorIndex(fz);
	stepx = gdx > 0.0f ? 1 : (gdx < 0.0f ? -1 : 0);
	stepz = gdz > 0.0f ? 1 : (gdz < 0.0f ? -1 : 0);

	/* Fraction along the segment where it leaves the current column/row;
	 * past 1 means never. */
	tmaxx = stepx > 0 ? ((f32)(cx + 1) - fx) / gdx : (stepx < 0 ? ((f32)cx - fx) / gdx : 2.0f);
	tmaxz = stepz > 0 ? ((f32)(cz + 1) - fz) / gdz : (stepz < 0 ? ((f32)cz - fz) / gdz : 2.0f);
	tdeltax = stepx != 0 ? 1.0f / meshAbs(gdx) : 2.0f;
	tdeltaz = stepz != 0 ? 1.0f / meshAbs(gdz) : 2.0f;

	/* The walk visits at most |gdx| + |gdz| + 1 cells. */
	steps = (s32)(meshAbs(gdx) + meshAbs(gdz)) + 4;

	while (steps-- > 0) {
		f32 texit;

		/* Cells below the origin hold nothing (the origin is the mesh
		 * minimum). Cells past the last index clamp to it, as
		 * meshWorldFinalize clamps insertion when the grid is capped. */
		if (cx >= 0 && cz >= 0) {
			const s32 ix = cx < grid->cellsx ? cx : grid->cellsx - 1;
			const s32 iz = cz < grid->cellsz ? cz : grid->cellsz - 1;

			meshGridTestCell(grid, iz * grid->cellsx + ix, from, to, mask,
				stamps, stamp, &best, &besttri);
		}

		texit = tmaxx < tmaxz ? tmaxx : tmaxz;

		/* A triangle is listed in every cell its XZ bounds overlap, so a hit
		 * in a later cell lies at or beyond this cell's exit. */
		if (best <= texit || texit > 1.0f) {
			break;
		}

		if (tmaxx < tmaxz) {
			cx += stepx;
			tmaxx += tdeltax;
		} else {
			cz += stepz;
			tmaxz += tdeltaz;
		}
	}

	if (besttri >= 0) {
		if (out_tri) {
			*out_tri = besttri;
		}
		if (out_normal) {
			*out_normal = grid->tris[besttri].normal;
		}
	}

	return best;
}

f32 meshTrisRayCast(const struct meshtri *tris, s32 numtris,
		const struct coord *from, const struct coord *to, u16 mask,
		struct coord *out_normal, s32 *out_tri)
{
	f32 best = 1.0f;
	s32 besttri = -1;
	s32 i;

	if (out_tri) {
		*out_tri = -1;
	}

	if (tris == NULL || from == NULL || to == NULL) {
		return 1.0f;
	}

	for (i = 0; i < numtris; i++) {
		f32 frac;

		if (mask != 0 && (tris[i].flags & mask) == 0) {
			continue;
		}

		if (meshRayVsTri(from, to, &tris[i], &frac) && frac < best) {
			best = frac;
			besttri = i;
		}
	}

	if (besttri >= 0) {
		if (out_tri) {
			*out_tri = besttri;
		}
		if (out_normal) {
			*out_normal = tris[besttri].normal;
		}
	}

	return best;
}

#define MESH_FLAT_WALK_MAX_DISTANCE 8000.0f
#define MESH_FLAT_WALK_MAX_DROP 500.0f
#define MESH_FLAT_WALK_EPSILON 0.25f
#define MESH_FLAT_WALK_RAIL_STEP 16.0f

static f32 meshMin(f32 a, f32 b)
{
	return a < b ? a : b;
}

static f32 meshMax(f32 a, f32 b)
{
	return a > b ? a : b;
}

/* A room of zero lets footprint rails use either room across a floor seam. */
static s32 meshFlatWalkFloor(const struct colmesh *mesh, const struct coord *above,
		f32 lower_y, RoomNum room, f32 *out_floor)
{
	if (!(above->y > lower_y)) return 0;

	struct coord below = *above;
	below.y = lower_y;
	f32 nearest = 1.0f;
	s32 found = 0;
	for (s32 i = 0; i < mesh->numtris; ++i) {
		const struct meshtri *tri = &mesh->tris[i];
		if ((room > 0 ? tri->roomnum != room : tri->roomnum <= 0)
				|| !(tri->flags & (GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2))
				|| (tri->flags & GEOFLAG_DIE) || !(tri->normal.y > 0.0f)) continue;
		const f32 y_min = meshMin(tri->v0.y, meshMin(tri->v1.y, tri->v2.y));
		const f32 y_max = meshMax(tri->v0.y, meshMax(tri->v1.y, tri->v2.y));
		if (!(y_max - y_min <= MESH_FLAT_WALK_EPSILON)
				|| y_max < lower_y || y_min > above->y) continue;
		f32 fraction;
		if (meshRayVsTri(above, &below, tri, &fraction) && fraction <= nearest) {
			if (!out_floor) return 1;
			nearest = fraction;
			found = 1;
		}
	}
	if (found) *out_floor = above->y + nearest * (lower_y - above->y);
	return found;
}

typedef struct mesh_walk_point {
	f64 axes[3];
} mesh_walk_point_t;

/* Clip the triangle against all six inclusive prism faces. Every plane
 * adds at most one vertex to a convex polygon (three plus six planes).
 * Strict crossings avoid emitting a boundary vertex twice. */
static s32 meshFlatWalkPrismIntersects(const mesh_walk_point_t triangle[3],
		const f64 minimum[3], const f64 maximum[3])
{
	enum { MAX_CLIP_VERTICES = 9 };
	mesh_walk_point_t buffer[2][MAX_CLIP_VERTICES];
	for (s32 i = 0; i < 3; ++i) buffer[0][i] = triangle[i];
	s32 count = 3;

	for (s32 plane = 0; plane < 6; ++plane) {
		const s32 axis = plane / 2;
		const s32 upper = plane % 2;
		const f64 bound = upper ? maximum[axis] : minimum[axis];
		const f64 sign = upper ? -1.0 : 1.0;
		const mesh_walk_point_t *input = buffer[upper];
		mesh_walk_point_t *output = buffer[1 - upper];
		s32 clipped = 0;
		const mesh_walk_point_t *previous = &input[count - 1];
		f64 previous_delta = sign * (previous->axes[axis] - bound);

		for (s32 i = 0; i < count; ++i) {
			const mesh_walk_point_t *current = &input[i];
			const f64 delta = sign * (current->axes[axis] - bound);
			if ((previous_delta < 0.0 && delta > 0.0) || (previous_delta > 0.0 && delta < 0.0)) {
				if (clipped == MAX_CLIP_VERTICES) return 1;
				const f64 fraction = previous_delta / (previous_delta - delta);
				mesh_walk_point_t *point = &output[clipped++];
				for (s32 component = 0; component < 3; ++component) {
					point->axes[component] = previous->axes[component]
						+ fraction * (current->axes[component] - previous->axes[component]);
				}
				point->axes[axis] = bound;
			}
			if (delta >= 0.0) {
				if (clipped == MAX_CLIP_VERTICES) return 1;
				output[clipped++] = *current;
			}
			previous = current;
			previous_delta = delta;
		}

		if (!clipped) return 0;
		count = clipped;
	}
	return 1;
}

static s32 meshFlatWalkPrismClear(const struct colmesh *mesh, const struct coord *origin,
		f32 ux, f32 uz, f32 length, f32 radius, f32 ground, f32 head)
{
	const f64 extent = (f64)radius + MESH_FLAT_WALK_EPSILON;
	const f64 maximum[3] = {(f64)length + extent, extent, head};
	for (s32 i = 0; i < mesh->numtris; ++i) {
		const struct meshtri *tri = &mesh->tris[i];
		const struct coord *verts[] = {&tri->v0, &tri->v1, &tri->v2};
		mesh_walk_point_t triangle[3];
		f64 tri_min[3] = {DBL_MAX, DBL_MAX, DBL_MAX};
		f64 tri_max[3] = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
		for (s32 corner = 0; corner < 3; ++corner) {
			const struct coord *v = verts[corner];
			const f64 dx = (f64)v->x - origin->x;
			const f64 dz = (f64)v->z - origin->z;
			triangle[corner] = (mesh_walk_point_t){{ux * dx + uz * dz, ux * dz - uz * dx, v->y}};
			for (s32 axis = 0; axis < 3; ++axis) {
				const f64 value = triangle[corner].axes[axis];
				if (!__builtin_isfinite(value)) return 0;
				if (value < tri_min[axis]) tri_min[axis] = value;
				if (value > tri_max[axis]) tri_max[axis] = value;
			}
		}
		const f64 bottom = (f64)ground + ((tri->flags & GEOFLAG_DIE)
			? -MESH_FLAT_WALK_EPSILON : MESH_FLAT_WALK_EPSILON);
		const f64 minimum[3] = {-extent, -extent, bottom};
		s32 axis = 0;
		while (axis < 3 && tri_max[axis] >= minimum[axis] && tri_min[axis] <= maximum[axis]) ++axis;
		if (axis == 3 && meshFlatWalkPrismIntersects(triangle, minimum, maximum)) return 0;
	}
	return 1;
}

s32 meshCheckFlatWalk(const struct colmesh *mesh, const struct coord *from, RoomNum from_room,
		const struct coord *to, RoomNum to_room, f32 radius, f32 height)
{
	if (!mesh || !mesh->tris || mesh->numtris <= 0 || !from || !to || from_room <= 0 || to_room <= 0
			|| !(radius > 0.0f && radius <= MESH_FLAT_WALK_MAX_DISTANCE)
			|| !(height > 0.0f) || !__builtin_isfinite(height)
			|| !__builtin_isfinite(from->y) || !__builtin_isfinite(to->y)) return 0;

	const f32 dx = to->x - from->x;
	const f32 dz = to->z - from->z;
	const f32 distance_sq = dx * dx + dz * dz;
	if (!(distance_sq <= MESH_FLAT_WALK_MAX_DISTANCE * MESH_FLAT_WALK_MAX_DISTANCE)) return 0;
	const f32 distance = sqrtf(distance_sq);
	const f32 ux = distance > 0.0f ? dx / distance : 1.0f;
	const f32 uz = distance > 0.0f ? dz / distance : 0.0f;

	f32 from_floor, to_floor;
	if (!meshFlatWalkFloor(mesh, from, from->y - MESH_FLAT_WALK_MAX_DROP, from_room, &from_floor)
			|| !meshFlatWalkFloor(mesh, to, to->y - MESH_FLAT_WALK_MAX_DROP, to_room, &to_floor)
			|| meshAbs(from_floor - to_floor) > MESH_FLAT_WALK_EPSILON) return 0;
	const f32 ground = meshMax(from_floor, to_floor);
	const f32 head = meshMax(meshMax(from->y, to->y), ground) + height;
	if (!__builtin_isfinite(head) || !(head > ground + MESH_FLAT_WALK_EPSILON)
			|| !meshFlatWalkPrismClear(mesh, from, ux, uz, distance, radius, ground, head)) return 0;

	const f32 rail_length = distance + 2.0f * radius;
	const s32 steps = (s32)(rail_length / MESH_FLAT_WALK_RAIL_STEP) + 1;
	for (s32 step = 0; step <= steps; ++step) {
		const f32 along = rail_length * (f32)step / (f32)steps - radius;
		for (s32 rail = -1; rail <= 1; ++rail) {
			const f32 across = (f32)rail * radius;
			const struct coord sample = {
				.x = from->x + ux * along - uz * across,
				.y = ground + MESH_FLAT_WALK_EPSILON,
				.z = from->z + uz * along + ux * across,
			};
			if (!meshFlatWalkFloor(mesh, &sample, ground - MESH_FLAT_WALK_EPSILON, 0, NULL)) return 0;
		}
	}
	return 1;
}
