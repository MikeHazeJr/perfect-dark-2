/**
 * meshcollision.h -- Mesh-based collision system
 *
 * Extracts actual polygon triangles from model display lists and stage
 * geometry for precise collision detection. Two categories:
 *
 *   Static mesh: all immovable geometry merged into one spatial grid at
 *                stage load. Walls, floors, ceilings, fixed props.
 *
 *   Dynamic mesh: per-prop triangle arrays for movable objects (doors,
 *                 lifts, platforms). Transformed by the prop's world
 *                 matrix at query time.
 *
 * The capsule sweep system (capsule.c) uses this for movement collision.
 * Damage/weapon collision stays on the legacy cdTestVolume path.
 */

#ifndef MESHCOLLISION_H
#define MESHCOLLISION_H

#include <ultra64.h>
#include "types.h"

/* ---- Triangle representation ---- */

struct meshtri {
	struct coord v0;
	struct coord v1;
	struct coord v2;
	struct coord normal;   /* precomputed face normal */
	u16 flags;             /* GEOFLAG_* for floor/wall/ceiling classification */
};

/* ---- Per-object collision mesh ---- */

struct colmesh {
	struct meshtri *tris;
	s32 numtris;
	s32 capacity;
	struct coord bboxmin;  /* local-space AABB for broad-phase */
	struct coord bboxmax;
};

/* ---- Spatial grid cell for static world mesh ---- */

#define MESHGRID_CELL_SIZE 256.0f  /* world units per cell */
#define MESHGRID_MAX_DIM   128     /* max cells per axis */

struct meshgridcell {
	s32 *triindices;       /* indices into the world mesh tri array */
	s32 count;
	s32 capacity;
};

struct meshgrid {
	struct meshtri *tris;
	s32 numtris;
	s32 tricapacity;
	struct meshgridcell *cells;
	s32 cellsx;
	s32 cellsz;
	f32 originx;
	f32 originz;
	f32 miny;
	f32 maxy;
	s32 ready;             /* 1 when built, 0 during construction */
};

/* ---- API: mesh extraction ---- */

/* Extract triangles from a model's display list nodes into a colmesh.
 * Walks the modelnode tree, finds DL nodes (type 0x18) and GUNDL nodes
 * (type 0x04), reads their Vtx arrays, and parses triangle commands
 * from the GBI display list to build the triangle array.
 * Caller owns the colmesh and must free with meshFree(). */
void meshExtractFromModel(struct model *model, struct colmesh *out);

/* Free a colmesh's triangle array */
void meshFree(struct colmesh *mesh);

/* ---- API: static world mesh ---- */

/* Initialize the world grid (call at stage load, before props) */
void meshWorldInit(void);

/* Add all triangles from a colmesh to the world grid, transformed by
 * the given matrix. For static props: extract mesh, transform, add,
 * then free the colmesh. */
void meshWorldAddMesh(struct colmesh *mesh, Mtxf *transform);

/* Add stage BG geometry (geotilei, geotilef) from room data */
void meshWorldAddRoomGeo(s32 roomnum);

/* Finalize the grid after all geometry is added (builds cell lists) */
void meshWorldFinalize(void);

/* Shut down and free all world mesh data (call at stage unload) */
void meshWorldShutdown(void);

/* ---- API: collision queries ---- */

/* Swept capsule vs world static mesh.
 * Returns fraction [0..1] of safe travel. Populates hit info. */
f32 meshSweepCapsuleWorld(struct coord *start, struct coord *move,
                          f32 radius, f32 halfheight,
                          struct coord *hitnormal, struct coord *hitpos);

/* Swept capsule vs a single dynamic colmesh (prop).
 * transform is the prop's current world matrix.
 * Returns fraction [0..1] of safe travel. */
f32 meshSweepCapsuleDynamic(struct coord *start, struct coord *move,
                            f32 radius, f32 halfheight,
                            struct colmesh *mesh, Mtxf *transform,
                            struct coord *hitnormal, struct coord *hitpos);

/* Find floor height below a position using mesh triangles.
 * Returns Y of highest floor surface below pos, or -30000 if none. */
f32 meshFindFloor(struct coord *pos, f32 radius, f32 *out_normalY);

/* Find ceiling height above a position using mesh triangles.
 * Returns Y of lowest ceiling surface above pos, or 99999 if none. */
f32 meshFindCeiling(struct coord *pos, f32 radius);

/* ---- API: per-prop dynamic mesh ---- */

/* Attach a collision mesh to a prop (for dynamic objects).
 * Extracted at prop creation, stays attached for lifetime. */
void meshAttachToProp(struct prop *prop, struct colmesh *mesh);

/* Get the collision mesh attached to a prop (NULL if none) */
struct colmesh *meshGetFromProp(struct prop *prop);

/* ---- Math utilities ---- */

/* Capsule vs triangle intersection test.
 * Returns 1 if intersection found, 0 otherwise.
 * If hit, fraction and normal are populated. */
s32 meshCapsuleVsTriangle(struct coord *capsuleA, struct coord *capsuleB,
                          f32 radius, struct coord *move,
                          struct meshtri *tri,
                          f32 *out_fraction, struct coord *out_normal);

/* Compute face normal from three vertices */
void meshComputeNormal(struct coord *v0, struct coord *v1, struct coord *v2,
                       struct coord *out_normal);

/* Global world grid instance */
extern struct meshgrid g_WorldMesh;

#endif /* MESHCOLLISION_H */
