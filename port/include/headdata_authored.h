#ifndef PD_HEADDATA_AUTHORED_H
#define PD_HEADDATA_AUTHORED_H

/*
 * port/include/headdata_authored.h -- Catalog Universality BYOR
 * Completion (2026-05-03).
 *
 * Public surface for the head AUTHORING source-of-truth.
 *
 * THIS HEADER IS FOR THE CATALOG REGISTRATION + RUNTIME EMITTER ONLY.
 * Engine code (gameplay, UI, networking, AI) MUST NOT include this
 * header; engine code reads head data through the catalog
 * (catalog_mgr_heads) which loads the per-asset .pdhead files
 * authored at startup by the romextract_pdhead emitter.
 *
 * Allowed includers:
 *   - port/src/headdata_authored.c        (the definitions)
 *   - port/src/assetcatalog_base.c        (catalog registration)
 *   - port/src/assetcatalog_base_extended.c (catalog mesh discovery)
 *   - port/src/catalog_mgr_heads.c        (manager mirror init)
 *   - port/src/romextract_pdhead.c        (the emitter)
 *   - port/src/romextract_pdmesh.c        (walks head mesh refs)
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>

/*
 * Schema mirrors the head subset of the legacy struct headorbody plus a
 * stable catalog_id slug for catalog registration and emitter filename
 * mint. No modeldef cache field -- the catalog manager owns that cache
 * separately.
 */
typedef struct {
	const char *catalog_id;   /* "base:head_carrington" / "base:sp_head_107" */
	s16  headnum;             /* historical g_HeadsAndBodies[] index */
	u8   ismale;
	u8   unk00_01;            /* always 1 for entries in this table (head subset) */
	u8   type;                /* HEADBODYTYPE_* */
	u16  height;
	u16  filenum;             /* CHEAD_* ROM file ID */
	f32  scale;
	f32  animscale;
} head_authored_record_t;

extern const head_authored_record_t g_HeadData[];
extern const s32 g_HeadDataCount;

/*
 * Lookup by historical g_HeadsAndBodies[] index. Linear scan; called
 * O(N_heads) times at catalog registration only. Returns NULL when no
 * head exists at that index (the legacy slot was a body or a sentinel).
 */
const head_authored_record_t *headDataLookupByHeadnum(s32 headnum);

#endif /* PD_HEADDATA_AUTHORED_H */
