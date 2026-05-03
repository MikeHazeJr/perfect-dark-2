#ifndef PD_ARENADATA_AUTHORED_H
#define PD_ARENADATA_AUTHORED_H

/*
 * port/include/arenadata_authored.h -- Catalog Universality BYOR
 * Completion (2026-05-03).
 *
 * Public surface for the arena AUTHORING source-of-truth.
 *
 * THIS HEADER IS FOR THE CATALOG REGISTRATION + RUNTIME EMITTER ONLY.
 * Engine code reads arena data through the catalog (catalog_mgr_arenas)
 * which loads the per-asset .pdarena + .pdscenario files authored at
 * startup by the romextract_pdarena emitter.
 *
 * Allowed includers:
 *   - port/src/arenadata_authored.c     (the definitions)
 *   - port/src/assetcatalog_base.c      (catalog registration)
 *   - port/src/catalog_mgr_arenas.c     (manager mirror init)
 *   - port/src/romextract_pdarena.c     (the emitter, also for .pdscenario)
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>

/*
 * Schema mirrors g_MpArenas[] entry plus slug + category strings (which
 * historically lived in s_ArenaNames[] and s_ArenaGroupMap[] inside
 * assetcatalog_base.c). load_mode is per-arena now (used to be derived
 * from the group via Solo Missions index range).
 */
typedef struct {
	const char *catalog_id;    /* "base:arena_mp_skedar" */
	const char *slug;          /* "mp_skedar" -- slug component of catalog_id */
	const char *category;      /* "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random" */
	s16  stagenum;             /* logical stage ID (STAGE_MP_*, STAGE_*) */
	u8   requirefeature;       /* MPFEATURE_CHR_* unlock gate */
	s32  name_langid;          /* langbank ID for display name */
	u8   load_mode;            /* ARENA_LOADMODE_PLAYABLE (0) / CANVAS (1) */
} arena_authored_record_t;

extern const arena_authored_record_t g_ArenaData[];
extern const s32 g_ArenaDataCount;

#endif /* PD_ARENADATA_AUTHORED_H */
