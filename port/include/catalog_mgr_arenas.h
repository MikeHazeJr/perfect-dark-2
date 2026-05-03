#ifndef PD_CATALOG_MGR_ARENAS_H
#define PD_CATALOG_MGR_ARENAS_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_arenas.h -- Catalog Gate 3 Arenas F1: Catalog
 * Manager for MP arenas.
 *
 * Background and design rationale: see
 *   context/audits/catalog-gate3-arenas-data-2026-05-02.md
 *
 * Mirrors the validated heads + bodies templates (catalog_mgr_heads.h,
 * catalog_mgr_bodies.h) and the F11-F13 weapons template. Phase 2
 * (F1-F11) status: this manager is a thin pass-through router that
 * mirrors the catalog row layer for ASSET_ARENA entries during the
 * parity period. F11 ships the per-asset envelope + Python extractor;
 * F12 wires the loader through s_get when active; F13 retires the
 * parity bridge.
 *
 * Logging: every miss path emits a CATALOG.MGR.ARENA.MISS: warning so
 * upstream callers can be diagnosed. Every override / load emits the
 * matching CATALOG.MGR.ARENA.* channel line.
 *
 * Arenas are read-only data with NO modeldef cache and NO mutators
 * (compare heads' modeldef + random-gender pickers and bodies'
 * canvaryheight + handfilenum). The API surface is therefore smaller
 * than heads / bodies.
 *
 * ARENA_LOADMODE_CANVAS invariant (B-254 Solo Missions Grid suppress):
 * carried per-arena via arena_data_t.load_mode. The hardcoded
 * `idx >= 13 && idx <= 26` check at registration migrates to a per-record
 * JSON value in F11.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Range bound for arena_index. Equals ARRAYCOUNT(g_MpArenas) post the
 * 2026-04-26 AllInOne / GEX cull. The legacy g_MpArenas[] is sized
 * identically (src/game/mplayer/setup.c:115 client + port/src/server_stubs.c:113
 * server stub). The slug shadow s_ArenaNames[] (port/src/assetcatalog_base.c:632)
 * matches.
 *
 * Group layout (the s_ArenaGroupMap[5] in assetcatalog_base.c):
 *   Dark           -> arena_index 0..12
 *   Solo Missions  -> arena_index 13..26
 *   Classic        -> arena_index 27..31
 *   Bonus          -> arena_index 32..44
 *   Random         -> arena_index 45..46
 */
#define CATALOG_MGR_ARENA_COUNT 47

/* Arenas typed payload (per audit Section B.2 / decision Ia.4).
 *
 * Mirrors the four ext.arena fields plus identity (arena_index,
 * catalog_id, slug, category). The slug + category strings duplicate
 * information already in the catalog ID and e->category, but having
 * them on the typed payload makes the manager self-contained for
 * diagnostic logging and for F12 round-trips through the loader.
 */
typedef struct arena_data {
	/* Identity */
	s16  arena_index;          /* runtime_index in catalog row, equal to slot in g_MpArenas[] / s_Arenas[] */
	char catalog_id[64];       /* "base:arena_mp_skedar" / "base:arena_test_lam" */
	char slug[32];             /* "mp_skedar" / "test_lam" -- slug component of catalog_id */
	char category[32];         /* "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random" */

	/* Arena fields (mirrors ext.arena) */
	s16  stagenum;             /* logical stage ID (STAGE_MP_*, STAGE_*) */
	u8   requirefeature;       /* MPFEATURE_CHR_* unlock gate */
	s32  name_langid;          /* langbank ID for display name */
	u8   load_mode;            /* ARENA_LOADMODE_PLAYABLE (0) / CANVAS (1) */
} arena_data_t;

/* Index-based hot-path accessor. O(1).
 * Returns NULL on out-of-range (loud miss log). */
const arena_data_t *catalogManagerGetArenaByIndex(s32 arena_index);

/* String-id accessor. Boundary use only; not a hot path.
 * Resolves catalog_id via assetCatalogResolve, expects ASSET_ARENA,
 * dereferences runtime_index back to the same arena entry as
 * catalogManagerGetArenaByIndex. */
const arena_data_t *catalogManagerGetArenaById(const char *catalog_id);

/* Iterator support: count of slots and accessor by 0-based iter index.
 * iter_index >= count returns NULL. The iterator surfaces ALL slots
 * (the manager pool is sized for the full arena range). Consumers that
 * want the enabled-filter (Universality Sweep B-303) iterate
 * assetCatalogIterateByType(ASSET_ARENA, ...) instead. */
s32 catalogManagerArenaCount(void);
const arena_data_t *catalogManagerGetArenaAt(s32 iter_index);

/* Lookup by stagenum. Returns the FIRST arena whose .stagenum matches.
 * Random meta arenas (STAGE_MP_RANDOM_MULTI / STAGE_MP_RANDOM_SOLO) are
 * special: their stagenum is a token; this lookup will return the meta
 * arena, not a resolution target. Callers wanting a real arena should
 * filter by category != "Random" before passing the stagenum. Returns
 * NULL if no arena owns the given stagenum. */
const arena_data_t *catalogManagerGetArenaByStagenum(s16 stagenum);

/* Lifecycle. */
void catalogManagerArenaInit(void);
void catalogManagerRegisterArena(const char *id, const arena_data_t *data);
void catalogManagerUnregisterArena(const char *id);
void catalogManagerArenaShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_ARENAS_H */
