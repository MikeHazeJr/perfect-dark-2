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

#include "lib/meshraygrid.h"

/* Local helpers instead of libm: the game supplies its own floorf
 * (src/game/floor.c), which pd-tests does not link. */
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
