/**
 * meshcollision.c -- Mesh-based collision system
 *
 * Extracts real polygon triangles from models and stage geometry,
 * builds a spatial grid for the static world, and provides swept
 * capsule-vs-triangle queries for the movement system.
 *
 * This file is C11 game code (not C++).
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include "constants.h"
#include "types.h"
#include "data.h"
#include "lib/meshcollision.h"
#include "game/prop.h"
#include "system.h"
#include "bss.h"

/* ======================================================================== */
/* Globals                                                                   */
/* ======================================================================== */

struct meshgrid g_WorldMesh;

/* ======================================================================== */
/* Internal helpers                                                          */
/* ======================================================================== */

static void meshGrow(struct colmesh *mesh, s32 needed)
{
	if (mesh->numtris + needed <= mesh->capacity) {
		return;
	}
	s32 newcap = mesh->capacity * 2;
	if (newcap < mesh->numtris + needed) {
		newcap = mesh->numtris + needed;
	}
	if (newcap < 64) {
		newcap = 64;
	}
	mesh->tris = realloc(mesh->tris, newcap * sizeof(struct meshtri));
	mesh->capacity = newcap;
}

static void gridCellAdd(struct meshgridcell *cell, s32 triindex)
{
	if (cell->count >= cell->capacity) {
		s32 newcap = cell->capacity * 2;
		if (newcap < 16) newcap = 16;
		cell->triindices = realloc(cell->triindices, newcap * sizeof(s32));
		cell->capacity = newcap;
	}
	cell->triindices[cell->count++] = triindex;
}

/* ======================================================================== */
/* Math utilities                                                            */
/* ======================================================================== */

void meshComputeNormal(struct coord *v0, struct coord *v1, struct coord *v2,
                       struct coord *out)
{
	f32 e1x = v1->x - v0->x;
	f32 e1y = v1->y - v0->y;
	f32 e1z = v1->z - v0->z;
	f32 e2x = v2->x - v0->x;
	f32 e2y = v2->y - v0->y;
	f32 e2z = v2->z - v0->z;

	out->x = e1y * e2z - e1z * e2y;
	out->y = e1z * e2x - e1x * e2z;
	out->z = e1x * e2y - e1y * e2x;

	f32 len = sqrtf(out->x * out->x + out->y * out->y + out->z * out->z);
	if (len > 0.0001f) {
		f32 inv = 1.0f / len;
		out->x *= inv;
		out->y *= inv;
		out->z *= inv;
	}
}

static f32 vec3Dot(struct coord *a, struct coord *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

static void vec3Sub(struct coord *a, struct coord *b, struct coord *out)
{
	out->x = a->x - b->x;
	out->y = a->y - b->y;
	out->z = a->z - b->z;
}

static void vec3Cross(struct coord *a, struct coord *b, struct coord *out)
{
	out->x = a->y * b->z - a->z * b->y;
	out->y = a->z * b->x - a->x * b->z;
	out->z = a->x * b->y - a->y * b->x;
}

static void addTriToMesh(struct colmesh *mesh,
                         f32 x0, f32 y0, f32 z0,
                         f32 x1, f32 y1, f32 z1,
                         f32 x2, f32 y2, f32 z2,
                         u16 flags)
{
	meshGrow(mesh, 1);
	struct meshtri *tri = &mesh->tris[mesh->numtris];
	tri->v0.x = x0; tri->v0.y = y0; tri->v0.z = z0;
	tri->v1.x = x1; tri->v1.y = y1; tri->v1.z = z1;
	tri->v2.x = x2; tri->v2.y = y2; tri->v2.z = z2;
	tri->flags = flags;
	meshComputeNormal(&tri->v0, &tri->v1, &tri->v2, &tri->normal);

	/* Update AABB */
	if (mesh->numtris == 0) {
		mesh->bboxmin = tri->v0;
		mesh->bboxmax = tri->v0;
	}
	for (s32 i = 0; i < 3; i++) {
		struct coord *v = (i == 0) ? &tri->v0 : (i == 1) ? &tri->v1 : &tri->v2;
		if (v->x < mesh->bboxmin.x) mesh->bboxmin.x = v->x;
		if (v->y < mesh->bboxmin.y) mesh->bboxmin.y = v->y;
		if (v->z < mesh->bboxmin.z) mesh->bboxmin.z = v->z;
		if (v->x > mesh->bboxmax.x) mesh->bboxmax.x = v->x;
		if (v->y > mesh->bboxmax.y) mesh->bboxmax.y = v->y;
		if (v->z > mesh->bboxmax.z) mesh->bboxmax.z = v->z;
	}

	mesh->numtris++;
}

/* Classify a triangle as floor, wall, or ceiling based on normal */
static u16 classifyTriFlags(struct coord *normal)
{
	/* Floor: normal pointing mostly up (Y > 0.7) */
	if (normal->y > 0.7f) {
		return GEOFLAG_FLOOR1;
	}
	/* Ceiling: normal pointing mostly down (Y < -0.7) */
	if (normal->y < -0.7f) {
		return GEOFLAG_FLOOR2; /* reuse floor2 for ceiling surfaces */
	}
	/* Wall: everything else */
	return GEOFLAG_WALL;
}

/* ======================================================================== */
/* Model triangle extraction                                                 */
/* ======================================================================== */

/**
 * Walk a model node tree and extract triangles from DL nodes (type 0x18).
 * The Vtx array in each DL node contains the vertices. The actual triangle
 * indices come from the GBI display list commands (gSP1Triangle, gSP2Triangles).
 * We parse the display list to find triangle commands and look up vertex
 * positions from the Vtx array.
 */
static void extractDLNodeTris(struct modelrodata_dl *dl, struct colmesh *mesh, f32 scale)
{
	if (!dl || !dl->vertices || !dl->opagdl) {
		return;
	}

	Vtx *vbuf = dl->vertices;
	s32 numverts = dl->numvertices;

	/* Temporary vertex buffer for GBI state tracking.
	 * gSPVertex loads vertices into slots; gSP1Triangle references slots. */
	struct coord slots[64];
	s32 numslots = 0;

	Gfx *gdl = dl->opagdl;
	if (!gdl) return;

	for (s32 cmdidx = 0; cmdidx < 4096; cmdidx++) {
		u32 w0 = gdl[cmdidx].words.w0;
		u32 w1 = gdl[cmdidx].words.w1;
		u8 cmd = (w0 >> 24) & 0xff;

		if (cmd == (u8)G_ENDDL) {
			break;
		}

		if (cmd == (u8)G_VTX) {
			/* gSPVertex: load N vertices starting at slot v0 */
			s32 n = ((w0 >> 4) & 0xf) + 1;
			s32 v0 = (w0 & 0xf);
			/* w1 is the address of the vertex data (points into vbuf) */
			Vtx *src = (Vtx *)(uintptr_t)w1;

			/* Calculate offset into the vertex array */
			if (src >= vbuf && src < vbuf + numverts) {
				s32 srcidx = (s32)(src - vbuf);
				for (s32 i = 0; i < n && (v0 + i) < 64; i++) {
					s32 vi = srcidx + i;
					if (vi < numverts) {
						slots[v0 + i].x = (f32)vbuf[vi].x * scale;
						slots[v0 + i].y = (f32)vbuf[vi].y * scale;
						slots[v0 + i].z = (f32)vbuf[vi].z * scale;
					}
				}
				if (v0 + n > numslots) numslots = v0 + n;
			}
		}

		if (cmd == (u8)G_TRI1) {
			/* gSP1Triangle: one triangle from 3 vertex slots.
			 * Vertex indices in w1, divided by 10 (PC port convention). */
			s32 i0 = ((w1 >> 16) & 0xff) / 10;
			s32 i1 = ((w1 >> 8)  & 0xff) / 10;
			s32 i2 = ((w1 >> 0)  & 0xff) / 10;
			if (i0 < numslots && i1 < numslots && i2 < numslots) {
				struct coord normal;
				meshComputeNormal(&slots[i0], &slots[i1], &slots[i2], &normal);
				addTriToMesh(mesh,
					slots[i0].x, slots[i0].y, slots[i0].z,
					slots[i1].x, slots[i1].y, slots[i1].z,
					slots[i2].x, slots[i2].y, slots[i2].z,
					classifyTriFlags(&normal));
			}
		}

		if (cmd == (u8)G_TRI4) {
			/* gSP4Triangles: up to 4 triangles packed into one command.
			 * Each 5-bit index pair in w0 and w1 specifies a triangle. */
			s32 idx[12];
			idx[0]  = (w0 >> 20) & 0x1f; idx[1]  = (w0 >> 15) & 0x1f; idx[2]  = (w0 >> 10) & 0x1f;
			idx[3]  = (w0 >> 5)  & 0x1f; idx[4]  = (w0 >> 0)  & 0x1f; idx[5]  = (w1 >> 27) & 0x1f;
			idx[6]  = (w1 >> 22) & 0x1f; idx[7]  = (w1 >> 17) & 0x1f; idx[8]  = (w1 >> 12) & 0x1f;
			idx[9]  = (w1 >> 7)  & 0x1f; idx[10] = (w1 >> 2)  & 0x1f; idx[11] = (w1 << 3)  & 0x1f;

			/* Up to 4 triangles, each uses 3 consecutive indices */
			for (s32 ti = 0; ti < 4; ti++) {
				s32 i0 = idx[ti * 3 + 0];
				s32 i1 = idx[ti * 3 + 1];
				s32 i2 = idx[ti * 3 + 2];
				/* Skip degenerate (all same index = padding) */
				if (i0 == i1 && i1 == i2) continue;
				if (i0 < numslots && i1 < numslots && i2 < numslots) {
					struct coord normal;
					meshComputeNormal(&slots[i0], &slots[i1], &slots[i2], &normal);
					addTriToMesh(mesh,
						slots[i0].x, slots[i0].y, slots[i0].z,
						slots[i1].x, slots[i1].y, slots[i1].z,
						slots[i2].x, slots[i2].y, slots[i2].z,
						classifyTriFlags(&normal));
				}
			}
		}
	}
}

static void extractNodeTreeTris(struct modelnode *node, struct colmesh *mesh, f32 scale)
{
	if (!node) return;

	if (node->type == 0x18 && node->rodata) {
		/* DL node -- has vertices and display list */
		extractDLNodeTris(&node->rodata->dl, mesh, scale);
	}

	/* Recurse into children */
	if (node->child) {
		extractNodeTreeTris(node->child, mesh, scale);
	}

	/* Recurse into siblings */
	if (node->next) {
		extractNodeTreeTris(node->next, mesh, scale);
	}
}

void meshExtractFromModel(struct model *model, struct colmesh *out)
{
	memset(out, 0, sizeof(*out));
	if (!model || !model->definition || !model->definition->rootnode) {
		return;
	}

	f32 scale = model->scale;
	if (scale <= 0.0f) scale = 1.0f;

	extractNodeTreeTris(model->definition->rootnode, out, scale);

	sysLogPrintf(LOG_NOTE, "MESHCOL: extracted %d triangles from model (scale %.2f)",
		out->numtris, scale);
}

void meshFree(struct colmesh *mesh)
{
	if (mesh->tris) {
		free(mesh->tris);
		mesh->tris = NULL;
	}
	mesh->numtris = 0;
	mesh->capacity = 0;
}

/* ======================================================================== */
/* Static world mesh                                                         */
/* ======================================================================== */

void meshWorldInit(void)
{
	memset(&g_WorldMesh, 0, sizeof(g_WorldMesh));
	g_WorldMesh.ready = 0;
}

void meshWorldAddMesh(struct colmesh *mesh, Mtxf *transform)
{
	if (!mesh || mesh->numtris == 0) return;

	/* Ensure capacity in world mesh */
	s32 needed = g_WorldMesh.numtris + mesh->numtris;
	if (needed > g_WorldMesh.tricapacity) {
		s32 newcap = g_WorldMesh.tricapacity * 2;
		if (newcap < needed) newcap = needed;
		if (newcap < 1024) newcap = 1024;
		g_WorldMesh.tris = realloc(g_WorldMesh.tris, newcap * sizeof(struct meshtri));
		g_WorldMesh.tricapacity = newcap;
	}

	for (s32 i = 0; i < mesh->numtris; i++) {
		struct meshtri *src = &mesh->tris[i];
		struct meshtri *dst = &g_WorldMesh.tris[g_WorldMesh.numtris];

		if (transform) {
			/* Transform vertices by the prop's world matrix */
			for (s32 vi = 0; vi < 3; vi++) {
				struct coord *sv = (vi == 0) ? &src->v0 : (vi == 1) ? &src->v1 : &src->v2;
				struct coord *dv = (vi == 0) ? &dst->v0 : (vi == 1) ? &dst->v1 : &dst->v2;
				dv->x = sv->x * transform->m[0][0] + sv->y * transform->m[1][0] + sv->z * transform->m[2][0] + transform->m[3][0];
				dv->y = sv->x * transform->m[0][1] + sv->y * transform->m[1][1] + sv->z * transform->m[2][1] + transform->m[3][1];
				dv->z = sv->x * transform->m[0][2] + sv->y * transform->m[1][2] + sv->z * transform->m[2][2] + transform->m[3][2];
			}
			meshComputeNormal(&dst->v0, &dst->v1, &dst->v2, &dst->normal);
			dst->flags = classifyTriFlags(&dst->normal);
		} else {
			*dst = *src;
		}

		g_WorldMesh.numtris++;
	}
}

void meshWorldAddRoomGeo(s32 roomnum)
{
	if (!g_TileFileData.u8 || !g_TileRooms) return;
	if (roomnum < 0 || roomnum >= g_TileNumRooms) return;

	u8 *ptr = g_TileFileData.u8 + g_TileRooms[roomnum];
	u8 *end = g_TileFileData.u8 + g_TileRooms[roomnum + 1];

	/* Temporary colmesh to collect room triangles */
	struct colmesh rmesh;
	memset(&rmesh, 0, sizeof(rmesh));

	while (ptr < end) {
		struct geo *geo = (struct geo *)ptr;
		s32 numverts = geo->numvertices;
		u16 flags = geo->flags;

		if (geo->type == GEOTYPE_TILE_I) {
			/* Integer-vertex BG tile */
			struct geotilei *tile = (struct geotilei *)ptr;
			/* Each tile is a polygon fan: vertex 0 is center, 1..n form the perimeter */
			for (s32 i = 1; i < numverts - 1; i++) {
				f32 x0 = (f32)tile->vertices[0][0];
				f32 y0 = (f32)tile->vertices[0][1];
				f32 z0 = (f32)tile->vertices[0][2];
				f32 x1 = (f32)tile->vertices[i][0];
				f32 y1 = (f32)tile->vertices[i][1];
				f32 z1 = (f32)tile->vertices[i][2];
				f32 x2 = (f32)tile->vertices[i + 1][0];
				f32 y2 = (f32)tile->vertices[i + 1][1];
				f32 z2 = (f32)tile->vertices[i + 1][2];
				struct coord normal;
				struct coord v0 = {x0, y0, z0};
				struct coord v1 = {x1, y1, z1};
				struct coord v2 = {x2, y2, z2};
				meshComputeNormal(&v0, &v1, &v2, &normal);
				addTriToMesh(&rmesh, x0, y0, z0, x1, y1, z1, x2, y2, z2,
					flags ? flags : classifyTriFlags(&normal));
			}
			ptr += 0x0e + numverts * 6; /* header(4) + floortype(2) + bbox(6) + floorcol(2) + verts */
		} else if (geo->type == GEOTYPE_TILE_F) {
			/* Float-vertex tile (lifts, moving platforms) */
			struct geotilef *tile = (struct geotilef *)ptr;
			for (s32 i = 1; i < numverts - 1; i++) {
				struct coord *v0 = &tile->vertices[0];
				struct coord *v1 = &tile->vertices[i];
				struct coord *v2 = &tile->vertices[i + 1];
				struct coord normal;
				meshComputeNormal(v0, v1, v2, &normal);
				addTriToMesh(&rmesh,
					v0->x, v0->y, v0->z,
					v1->x, v1->y, v1->z,
					v2->x, v2->y, v2->z,
					flags ? flags : classifyTriFlags(&normal));
			}
			ptr += 0x10 + numverts * 12; /* header(4) + floortype(2) + bbox(6) + floorcol(2) + pad(2) + verts */
		} else if (geo->type == GEOTYPE_BLOCK) {
			/* Block: XZ polygon with ymin/ymax. Convert to triangles. */
			struct geoblock *block = (struct geoblock *)ptr;
			/* Create two triangles for each quad face of the block */
			for (s32 i = 0; i < numverts; i++) {
				s32 j = (i + 1) % numverts;
				f32 x0 = block->vertices[i][0], z0 = block->vertices[i][1];
				f32 x1 = block->vertices[j][0], z1 = block->vertices[j][1];
				/* Side wall: two triangles */
				addTriToMesh(&rmesh,
					x0, block->ymin, z0, x1, block->ymin, z1, x1, block->ymax, z1,
					GEOFLAG_WALL);
				addTriToMesh(&rmesh,
					x0, block->ymin, z0, x1, block->ymax, z1, x0, block->ymax, z0,
					GEOFLAG_WALL);
			}
			/* Top face (floor) */
			for (s32 i = 1; i < numverts - 1; i++) {
				addTriToMesh(&rmesh,
					block->vertices[0][0], block->ymax, block->vertices[0][1],
					block->vertices[i][0], block->ymax, block->vertices[i][1],
					block->vertices[i+1][0], block->ymax, block->vertices[i+1][1],
					GEOFLAG_FLOOR1);
			}
			ptr += 0x0c + numverts * 8;
		} else if (geo->type == GEOTYPE_CYL) {
			/* Cylinder: skip for now (chrs use geocyl, not worth triangulating) */
			ptr += sizeof(struct geocyl);
		} else {
			/* Unknown type: skip by guessing stride */
			break;
		}
	}

	if (rmesh.numtris > 0) {
		meshWorldAddMesh(&rmesh, NULL);
	}
	meshFree(&rmesh);
}

void meshWorldFinalize(void)
{
	if (g_WorldMesh.numtris == 0) {
		g_WorldMesh.ready = 1;
		return;
	}

	/* Compute world bounds */
	f32 minx = g_WorldMesh.tris[0].v0.x, maxx = minx;
	f32 minz = g_WorldMesh.tris[0].v0.z, maxz = minz;
	f32 miny = g_WorldMesh.tris[0].v0.y, maxy = miny;

	for (s32 i = 0; i < g_WorldMesh.numtris; i++) {
		struct meshtri *t = &g_WorldMesh.tris[i];
		for (s32 vi = 0; vi < 3; vi++) {
			struct coord *v = (vi == 0) ? &t->v0 : (vi == 1) ? &t->v1 : &t->v2;
			if (v->x < minx) minx = v->x;
			if (v->x > maxx) maxx = v->x;
			if (v->z < minz) minz = v->z;
			if (v->z > maxz) maxz = v->z;
			if (v->y < miny) miny = v->y;
			if (v->y > maxy) maxy = v->y;
		}
	}

	g_WorldMesh.originx = minx;
	g_WorldMesh.originz = minz;
	g_WorldMesh.miny = miny;
	g_WorldMesh.maxy = maxy;

	g_WorldMesh.cellsx = (s32)((maxx - minx) / MESHGRID_CELL_SIZE) + 1;
	g_WorldMesh.cellsz = (s32)((maxz - minz) / MESHGRID_CELL_SIZE) + 1;
	if (g_WorldMesh.cellsx > MESHGRID_MAX_DIM) g_WorldMesh.cellsx = MESHGRID_MAX_DIM;
	if (g_WorldMesh.cellsz > MESHGRID_MAX_DIM) g_WorldMesh.cellsz = MESHGRID_MAX_DIM;

	s32 totalcells = g_WorldMesh.cellsx * g_WorldMesh.cellsz;
	g_WorldMesh.cells = calloc(totalcells, sizeof(struct meshgridcell));

	/* Insert each triangle into all cells it overlaps */
	for (s32 i = 0; i < g_WorldMesh.numtris; i++) {
		struct meshtri *t = &g_WorldMesh.tris[i];

		/* Triangle AABB */
		f32 txmin = t->v0.x, txmax = t->v0.x;
		f32 tzmin = t->v0.z, tzmax = t->v0.z;
		for (s32 vi = 1; vi < 3; vi++) {
			struct coord *v = (vi == 1) ? &t->v1 : &t->v2;
			if (v->x < txmin) txmin = v->x;
			if (v->x > txmax) txmax = v->x;
			if (v->z < tzmin) tzmin = v->z;
			if (v->z > tzmax) tzmax = v->z;
		}

		s32 cx0 = (s32)((txmin - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
		s32 cx1 = (s32)((txmax - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
		s32 cz0 = (s32)((tzmin - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);
		s32 cz1 = (s32)((tzmax - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);

		if (cx0 < 0) cx0 = 0;
		if (cz0 < 0) cz0 = 0;
		if (cx1 >= g_WorldMesh.cellsx) cx1 = g_WorldMesh.cellsx - 1;
		if (cz1 >= g_WorldMesh.cellsz) cz1 = g_WorldMesh.cellsz - 1;

		for (s32 cz = cz0; cz <= cz1; cz++) {
			for (s32 cx = cx0; cx <= cx1; cx++) {
				gridCellAdd(&g_WorldMesh.cells[cz * g_WorldMesh.cellsx + cx], i);
			}
		}
	}

	g_WorldMesh.ready = 1;

	sysLogPrintf(LOG_NOTE, "MESHCOL: world mesh finalized: %d tris, %dx%d grid (%d cells)",
		g_WorldMesh.numtris, g_WorldMesh.cellsx, g_WorldMesh.cellsz, totalcells);
}

void meshWorldShutdown(void)
{
	if (g_WorldMesh.cells) {
		s32 totalcells = g_WorldMesh.cellsx * g_WorldMesh.cellsz;
		for (s32 i = 0; i < totalcells; i++) {
			if (g_WorldMesh.cells[i].triindices) {
				free(g_WorldMesh.cells[i].triindices);
			}
		}
		free(g_WorldMesh.cells);
	}
	if (g_WorldMesh.tris) {
		free(g_WorldMesh.tris);
	}
	memset(&g_WorldMesh, 0, sizeof(g_WorldMesh));
}

/* ======================================================================== */
/* Capsule vs triangle intersection                                          */
/* ======================================================================== */

/**
 * Test if a swept capsule intersects a triangle.
 *
 * The capsule is defined by two endpoints (A, B) and a radius.
 * The sweep direction is 'move'. We find the earliest time t in [0,1]
 * where the capsule contacts the triangle plane, then verify the contact
 * point is inside the triangle (or within radius of an edge/vertex).
 *
 * This is a simplified version that tests the capsule's central axis
 * (as a ray) against the triangle inflated by the capsule radius.
 * Good enough for gameplay collision; not pixel-perfect.
 */
s32 meshCapsuleVsTriangle(struct coord *capsuleA, struct coord *capsuleB,
                          f32 radius, struct coord *move,
                          struct meshtri *tri,
                          f32 *out_fraction, struct coord *out_normal)
{
	/* Use the capsule midpoint as the sweep origin */
	struct coord origin;
	origin.x = (capsuleA->x + capsuleB->x) * 0.5f;
	origin.y = (capsuleA->y + capsuleB->y) * 0.5f;
	origin.z = (capsuleA->z + capsuleB->z) * 0.5f;

	/* Test sphere (at midpoint, radius = capsule radius + half height) sweep
	 * against triangle plane offset by radius */
	f32 denom = vec3Dot(&tri->normal, move);
	if (fabsf(denom) < 0.0001f) {
		return 0; /* parallel to triangle */
	}

	struct coord toplane;
	vec3Sub(&tri->v0, &origin, &toplane);
	f32 planedist = vec3Dot(&tri->normal, &toplane);

	/* Offset by radius in the direction of the normal */
	f32 signedRadius = (denom < 0.0f) ? radius : -radius;
	f32 t = (planedist + signedRadius) / denom;

	if (t < 0.0f || t > 1.0f) {
		return 0; /* intersection outside sweep range */
	}

	/* Compute contact point on the plane */
	struct coord contact;
	contact.x = origin.x + move->x * t;
	contact.y = origin.y + move->y * t;
	contact.z = origin.z + move->z * t;

	/* Check if contact point is inside the triangle (with radius tolerance) */
	/* Use barycentric coordinates with edge distance fallback */
	struct coord e0, e1, e2, c0, c1, c2;
	vec3Sub(&tri->v1, &tri->v0, &e0);
	vec3Sub(&tri->v2, &tri->v0, &e1);
	vec3Sub(&contact, &tri->v0, &c0);

	f32 d00 = vec3Dot(&e0, &e0);
	f32 d01 = vec3Dot(&e0, &e1);
	f32 d11 = vec3Dot(&e1, &e1);
	f32 d20 = vec3Dot(&c0, &e0);
	f32 d21 = vec3Dot(&c0, &e1);

	f32 det = d00 * d11 - d01 * d01;
	if (fabsf(det) < 0.0001f) return 0;

	f32 invdet = 1.0f / det;
	f32 u = (d11 * d20 - d01 * d21) * invdet;
	f32 v = (d00 * d21 - d01 * d20) * invdet;

	/* Slack proportional to capsule radius vs triangle size */
	f32 avgEdge = sqrtf((d00 + d11) * 0.5f);
	f32 slack = (avgEdge > 0.1f) ? (radius / avgEdge) : 0.1f;
	if (slack < 0.05f) slack = 0.05f;
	if (u >= -slack && v >= -slack && (u + v) <= 1.0f + slack) {
		*out_fraction = t;
		*out_normal = tri->normal;
		return 1;
	}

	return 0;
}

/* ======================================================================== */
/* Swept capsule queries                                                     */
/* ======================================================================== */

f32 meshSweepCapsuleWorld(struct coord *start, struct coord *move,
                          f32 radius, f32 halfheight,
                          struct coord *hitnormal, struct coord *hitpos)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) {
		return 1.0f;
	}

	struct coord capsuleA, capsuleB;
	capsuleA.x = start->x; capsuleA.y = start->y - halfheight; capsuleA.z = start->z;
	capsuleB.x = start->x; capsuleB.y = start->y + halfheight; capsuleB.z = start->z;

	/* Determine which grid cells the sweep passes through */
	f32 sweepminx = start->x, sweepmaxx = start->x;
	f32 sweepminz = start->z, sweepmaxz = start->z;
	f32 endx = start->x + move->x, endz = start->z + move->z;
	if (endx < sweepminx) sweepminx = endx;
	if (endx > sweepmaxx) sweepmaxx = endx;
	if (endz < sweepminz) sweepminz = endz;
	if (endz > sweepmaxz) sweepmaxz = endz;
	sweepminx -= radius; sweepmaxx += radius;
	sweepminz -= radius; sweepmaxz += radius;

	s32 cx0 = (s32)((sweepminx - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
	s32 cx1 = (s32)((sweepmaxx - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
	s32 cz0 = (s32)((sweepminz - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);
	s32 cz1 = (s32)((sweepmaxz - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);

	if (cx0 < 0) cx0 = 0;
	if (cz0 < 0) cz0 = 0;
	if (cx1 >= g_WorldMesh.cellsx) cx1 = g_WorldMesh.cellsx - 1;
	if (cz1 >= g_WorldMesh.cellsz) cz1 = g_WorldMesh.cellsz - 1;

	f32 bestfrac = 1.0f;
	struct coord bestnormal = {0, 1, 0};

	/* Track which triangles we've already tested (avoid duplicates across cells) */
	/* Simple approach: skip if we've seen this tri index before in this sweep */
	/* For performance, we use a small inline set */
	s32 tested[512];
	s32 numtested = 0;

	for (s32 cz = cz0; cz <= cz1; cz++) {
		for (s32 cx = cx0; cx <= cx1; cx++) {
			struct meshgridcell *cell = &g_WorldMesh.cells[cz * g_WorldMesh.cellsx + cx];
			for (s32 ci = 0; ci < cell->count; ci++) {
				s32 tidx = cell->triindices[ci];

				/* Dedup check */
				s32 dup = 0;
				for (s32 di = 0; di < numtested && di < 512; di++) {
					if (tested[di] == tidx) { dup = 1; break; }
				}
				if (dup) continue;
				if (numtested < 512) tested[numtested++] = tidx;

				f32 frac;
				struct coord normal;
				if (meshCapsuleVsTriangle(&capsuleA, &capsuleB, radius, move,
				                          &g_WorldMesh.tris[tidx], &frac, &normal)) {
					if (frac < bestfrac) {
						bestfrac = frac;
						bestnormal = normal;
					}
				}
			}
		}
	}

	if (hitnormal) *hitnormal = bestnormal;
	if (hitpos) {
		hitpos->x = start->x + move->x * bestfrac;
		hitpos->y = start->y + move->y * bestfrac;
		hitpos->z = start->z + move->z * bestfrac;
	}

	return bestfrac;
}

f32 meshSweepCapsuleDynamic(struct coord *start, struct coord *move,
                            f32 radius, f32 halfheight,
                            struct colmesh *mesh, Mtxf *transform,
                            struct coord *hitnormal, struct coord *hitpos)
{
	if (!mesh || mesh->numtris == 0) {
		return 1.0f;
	}

	struct coord capsuleA, capsuleB;
	capsuleA.x = start->x; capsuleA.y = start->y - halfheight; capsuleA.z = start->z;
	capsuleB.x = start->x; capsuleB.y = start->y + halfheight; capsuleB.z = start->z;

	f32 bestfrac = 1.0f;
	struct coord bestnormal = {0, 1, 0};

	for (s32 i = 0; i < mesh->numtris; i++) {
		struct meshtri worldtri;
		struct meshtri *src = &mesh->tris[i];

		/* Transform triangle vertices by prop's world matrix */
		if (transform) {
			for (s32 vi = 0; vi < 3; vi++) {
				struct coord *sv = (vi == 0) ? &src->v0 : (vi == 1) ? &src->v1 : &src->v2;
				struct coord *dv = (vi == 0) ? &worldtri.v0 : (vi == 1) ? &worldtri.v1 : &worldtri.v2;
				dv->x = sv->x * transform->m[0][0] + sv->y * transform->m[1][0] + sv->z * transform->m[2][0] + transform->m[3][0];
				dv->y = sv->x * transform->m[0][1] + sv->y * transform->m[1][1] + sv->z * transform->m[2][1] + transform->m[3][1];
				dv->z = sv->x * transform->m[0][2] + sv->y * transform->m[1][2] + sv->z * transform->m[2][2] + transform->m[3][2];
			}
			meshComputeNormal(&worldtri.v0, &worldtri.v1, &worldtri.v2, &worldtri.normal);
		} else {
			worldtri = *src;
		}

		f32 frac;
		struct coord normal;
		if (meshCapsuleVsTriangle(&capsuleA, &capsuleB, radius, move,
		                          &worldtri, &frac, &normal)) {
			if (frac < bestfrac) {
				bestfrac = frac;
				bestnormal = normal;
			}
		}
	}

	if (hitnormal) *hitnormal = bestnormal;
	if (hitpos) {
		hitpos->x = start->x + move->x * bestfrac;
		hitpos->y = start->y + move->y * bestfrac;
		hitpos->z = start->z + move->z * bestfrac;
	}

	return bestfrac;
}

/* ======================================================================== */
/* Dynamic mesh attachment                                                   */
/* ======================================================================== */

void meshAttachToProp(struct prop *prop, struct colmesh *mesh)
{
	if (prop) {
		prop->colmesh = mesh;
	}
}

struct colmesh *meshGetFromProp(struct prop *prop)
{
	if (prop) {
		return prop->colmesh;
	}
	return NULL;
}

/* ======================================================================== */
/* Mesh-based floor and ceiling queries                                      */
/* ======================================================================== */

/**
 * Find the floor height below a position by casting a ray downward against
 * all floor-classified triangles in the nearby grid cells.
 * Returns the Y coordinate of the highest floor surface below pos.
 */
f32 meshFindFloor(struct coord *pos, f32 radius, f32 *out_normalY)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) {
		if (out_normalY) *out_normalY = 1.0f;
		return -30000.0f;
	}

	/* Find the grid cell at this XZ position */
	s32 cx = (s32)((pos->x - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
	s32 cz = (s32)((pos->z - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);

	f32 bestFloorY = -30000.0f;
	f32 bestNormalY = 1.0f;

	/* Check a 3x3 neighborhood of cells for nearby floor triangles */
	for (s32 dz = -1; dz <= 1; dz++) {
		for (s32 dx = -1; dx <= 1; dx++) {
			s32 gx = cx + dx;
			s32 gz = cz + dz;
			if (gx < 0 || gz < 0 || gx >= g_WorldMesh.cellsx || gz >= g_WorldMesh.cellsz) continue;

			struct meshgridcell *cell = &g_WorldMesh.cells[gz * g_WorldMesh.cellsx + gx];
			for (s32 ci = 0; ci < cell->count; ci++) {
				struct meshtri *tri = &g_WorldMesh.tris[cell->triindices[ci]];

				/* Only test floor-like triangles (normal pointing up) */
				if (tri->normal.y < 0.5f) continue;

				/* Check if XZ position is within the triangle (projected) */
				/* Barycentric test in XZ plane */
				f32 x = pos->x, z = pos->z;
				f32 x0 = tri->v0.x, z0 = tri->v0.z;
				f32 x1 = tri->v1.x, z1 = tri->v1.z;
				f32 x2 = tri->v2.x, z2 = tri->v2.z;

				f32 d00 = (x1-x0)*(x1-x0) + (z1-z0)*(z1-z0);
				f32 d01 = (x1-x0)*(x2-x0) + (z1-z0)*(z2-z0);
				f32 d11 = (x2-x0)*(x2-x0) + (z2-z0)*(z2-z0);
				f32 d20 = (x-x0)*(x1-x0)  + (z-z0)*(z1-z0);
				f32 d21 = (x-x0)*(x2-x0)  + (z-z0)*(z2-z0);

				f32 det = d00 * d11 - d01 * d01;
				if (fabsf(det) < 0.001f) continue;
				f32 inv = 1.0f / det;
				f32 u = (d11 * d20 - d01 * d21) * inv;
				f32 v = (d00 * d21 - d01 * d20) * inv;

				/* Slack proportional to player radius vs triangle size */
				f32 avgEdge = sqrtf((d00 + d11) * 0.5f);
				f32 slack = (avgEdge > 0.1f) ? (radius / avgEdge) : 0.1f;
				if (slack < 0.05f) slack = 0.05f;
				if (u < -slack || v < -slack || (u + v) > 1.0f + slack) continue;

				/* Compute Y on the triangle plane at this XZ */
				/* Plane equation: N.x*(X-V0.x) + N.y*(Y-V0.y) + N.z*(Z-V0.z) = 0 */
				/* Solve for Y: Y = V0.y - (N.x*(X-V0.x) + N.z*(Z-V0.z)) / N.y */
				if (fabsf(tri->normal.y) < 0.001f) continue;
				f32 floorY = tri->v0.y - (tri->normal.x * (x - tri->v0.x) + tri->normal.z * (z - tri->v0.z)) / tri->normal.y;

				/* Must be below the query position */
				if (floorY <= pos->y && floorY > bestFloorY) {
					bestFloorY = floorY;
					bestNormalY = tri->normal.y;
				}
			}
		}
	}

	if (out_normalY) *out_normalY = bestNormalY;
	return bestFloorY;
}

/**
 * Find the ceiling height above a position by checking all ceiling-classified
 * triangles in the nearby grid cells.
 * Returns the Y coordinate of the lowest ceiling surface above pos.
 */
f32 meshFindCeiling(struct coord *pos, f32 radius)
{
	if (!g_WorldMesh.ready || g_WorldMesh.numtris == 0) {
		return 99999.0f;
	}

	s32 cx = (s32)((pos->x - g_WorldMesh.originx) / MESHGRID_CELL_SIZE);
	s32 cz = (s32)((pos->z - g_WorldMesh.originz) / MESHGRID_CELL_SIZE);

	f32 bestCeilY = 99999.0f;

	for (s32 dz = -1; dz <= 1; dz++) {
		for (s32 dx = -1; dx <= 1; dx++) {
			s32 gx = cx + dx;
			s32 gz = cz + dz;
			if (gx < 0 || gz < 0 || gx >= g_WorldMesh.cellsx || gz >= g_WorldMesh.cellsz) continue;

			struct meshgridcell *cell = &g_WorldMesh.cells[gz * g_WorldMesh.cellsx + gx];
			for (s32 ci = 0; ci < cell->count; ci++) {
				struct meshtri *tri = &g_WorldMesh.tris[cell->triindices[ci]];

				f32 x = pos->x, z = pos->z;
				f32 x0 = tri->v0.x, z0 = tri->v0.z;
				f32 x1 = tri->v1.x, z1 = tri->v1.z;
				f32 x2 = tri->v2.x, z2 = tri->v2.z;

				f32 d00 = (x1-x0)*(x1-x0) + (z1-z0)*(z1-z0);
				f32 d01 = (x1-x0)*(x2-x0) + (z1-z0)*(z2-z0);
				f32 d11 = (x2-x0)*(x2-x0) + (z2-z0)*(z2-z0);
				f32 d20 = (x-x0)*(x1-x0)  + (z-z0)*(z1-z0);
				f32 d21 = (x-x0)*(x2-x0)  + (z-z0)*(z2-z0);

				f32 det = d00 * d11 - d01 * d01;
				if (fabsf(det) < 0.001f) continue;
				f32 inv = 1.0f / det;
				f32 u = (d11 * d20 - d01 * d21) * inv;
				f32 v = (d00 * d21 - d01 * d20) * inv;

				/* Use a generous slack based on player radius vs triangle size.
				 * The barycentric coords range [0,1], so slack = radius / avg_edge_length
				 * gives us the right proportional tolerance. */
				f32 avgEdge = sqrtf((d00 + d11) * 0.5f);
				f32 slack = (avgEdge > 0.1f) ? (radius / avgEdge) : 0.1f;
				if (slack < 0.05f) slack = 0.05f;
				if (u < -slack || v < -slack || (u + v) > 1.0f + slack) continue;

				/* Compute Y at this XZ on the triangle plane */
				f32 ny = tri->normal.y;
				if (fabsf(ny) < 0.001f) continue;
				f32 surfY = tri->v0.y - (tri->normal.x * (x - tri->v0.x) + tri->normal.z * (z - tri->v0.z)) / ny;

				/* Must be above the query position */
				if (surfY >= pos->y && surfY < bestCeilY) {
					bestCeilY = surfY;
				}
			}
		}
	}

	return bestCeilY;
}
