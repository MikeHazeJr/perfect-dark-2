#ifndef PD_BODYDATA_AUTHORED_H
#define PD_BODYDATA_AUTHORED_H

/*
 * port/include/bodydata_authored.h -- Catalog Universality BYOR
 * Completion (2026-05-03).
 *
 * Public surface for the body AUTHORING source-of-truth.
 *
 * THIS HEADER IS FOR THE CATALOG REGISTRATION + RUNTIME EMITTER ONLY.
 * Engine code reads body data through the catalog (catalog_mgr_bodies)
 * which loads the per-asset .pdbody files authored at startup by the
 * romextract_pdbody emitter.
 *
 * Allowed includers:
 *   - port/src/bodydata_authored.c          (the definitions)
 *   - port/src/assetcatalog_base.c          (catalog registration)
 *   - port/src/assetcatalog_base_extended.c (catalog mesh discovery)
 *   - port/src/assetcatalog_api.c           (handfilenum sentinel reads)
 *   - port/src/catalog_mgr_bodies.c         (manager mirror init)
 *   - port/src/romextract_pdbody.c          (the emitter)
 *   - port/src/romextract_pdmesh.c          (walks body+hand mesh refs)
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>

/*
 * Schema mirrors the body subset of the legacy struct headorbody plus a
 * stable catalog_id slug. Includes handfilenum (first-person hand model;
 * B-275 dependency). No modeldef cache field -- the catalog manager
 * owns that cache separately.
 */
typedef struct {
	const char *catalog_id;   /* "base:dark_combat" / "base:sp_body_107" */
	s16  bodynum;             /* historical g_HeadsAndBodies[] index */
	u8   ismale;
	u8   unk00_01;            /* 1 = integrated-head body (Skedar/Dr Caroll/EyeSpy); 0 = normal */
	u8   canvaryheight;       /* 1 = per-chr height variance (Skedar) */
	u8   type;                /* HEADBODYTYPE_* */
	u16  height;
	u16  filenum;             /* CBODY_* ROM file ID */
	f32  scale;
	f32  animscale;
	u16  handfilenum;         /* GHAND_* first-person hand model file ID; 0 if none */
} body_authored_record_t;

extern const body_authored_record_t g_BodyData[];
extern const s32 g_BodyDataCount;

/*
 * Lookup by historical g_HeadsAndBodies[] index. Linear scan; called
 * O(N_bodies) times at catalog registration only. Returns NULL when no
 * body exists at that index (the legacy slot was a head or a sentinel).
 */
const body_authored_record_t *bodyDataLookupByBodynum(s32 bodynum);

#endif /* PD_BODYDATA_AUTHORED_H */
