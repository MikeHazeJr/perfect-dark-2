/*
 * port/src/catalog_mgr_arenas.c -- Catalog Gate 3 Arenas F1: Catalog
 * Manager for MP arenas.
 *
 * See port/include/catalog_mgr_arenas.h for the public contract and
 * context/audits/catalog-gate3-arenas-data-2026-05-02.md for the design.
 *
 * Phase 2 (F1-F11): this manager mirrors the catalog row layer for
 * ASSET_ARENA entries. Init walks every ASSET_ARENA catalog row,
 * extracts the typed payload from `e->ext.arena` + `e->category` +
 * `e->id`, and stores it in s_Arenas[]. F12 wires the loader_pool
 * via loaderPoolArenasActive() check in s_get; F13 retires the
 * parity bridge.
 *
 * Pure validators live in port/src/catalog_mgr_arenas_pure.c and are
 * pinned by tests/test_catalog_mgr_arenas_api.cpp.
 */

#include <ultra64.h>
#include <stddef.h>
#include <string.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "system.h"
#include "assetcatalog.h"
#include "catalog_mgr_arenas.h"
#include "catalog_mgr_arenas_pure.h"
#include "loader_pool.h"  /* Catalog Gate 3 Arenas F12: pool-backed source when active */

/* F1 backing pool: a parallel mirror that the manager owns. Populated
 * during init by walking ASSET_ARENA catalog rows; F12 switches the
 * source to the loader pool. */
static arena_data_t s_Arenas[CATALOG_MGR_ARENA_COUNT];
static s32 s_ArenasInited = 0;

/* Init-time iterator: walks every ASSET_ARENA entry, populates the
 * matching s_Arenas[arena_index] slot. */
static void s_initCollectCb(const asset_entry_t *entry, void *userdata)
{
	s32 idx;
	arena_data_t *dst;
	const char *slug;

	(void)userdata;
	if (entry == NULL || entry->type != ASSET_ARENA) {
		return;
	}

	idx = entry->runtime_index;
	if (!catalogMgrArenaIsInRangePure(idx)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.ARENA.MISS: init id=\"%s\" runtime_index=%d out of range",
			entry->id, idx);
		return;
	}

	dst = &s_Arenas[idx];
	dst->arena_index = (s16)idx;

	strncpy(dst->catalog_id, entry->id, sizeof(dst->catalog_id) - 1);
	dst->catalog_id[sizeof(dst->catalog_id) - 1] = '\0';

	/* Slug: strip the "<ns>:arena_" prefix from the catalog ID. */
	slug = catalogMgrArenaSlugFromIdPure(entry->id);
	if (slug != NULL) {
		strncpy(dst->slug, slug, sizeof(dst->slug) - 1);
		dst->slug[sizeof(dst->slug) - 1] = '\0';
	} else {
		dst->slug[0] = '\0';
	}

	strncpy(dst->category, entry->category, sizeof(dst->category) - 1);
	dst->category[sizeof(dst->category) - 1] = '\0';

	dst->stagenum = (s16)entry->ext.arena.stagenum;
	dst->requirefeature = (u8)entry->ext.arena.requirefeature;
	dst->name_langid = entry->ext.arena.name_langid;
	dst->load_mode = entry->ext.arena.load_mode;
}

static const arena_data_t *s_get(s32 arena_index)
{
	if (!catalogMgrArenaIsInRangePure(arena_index)) {
		return NULL;
	}
	if (!s_ArenasInited) {
		catalogManagerArenaInit();
	}

#if !defined(PD_SERVER)
	/* Catalog Gate 3 Arenas F12: when the loader is active, the
	 * loader_pool is the source of truth. Copy the loader-owned
	 * record into the manager slot so all accessors continue to read
	 * from s_Arenas[]. F13 retires the legacy mirror entirely; for
	 * now we keep both paths so the parity bridge can be exercised.
	 *
	 * pd-server does not link loader_pool.c (no per-asset reading
	 * server-side), so this branch compiles out. The catalog-row-
	 * derived mirror populated at init is the only source on the
	 * server. */
	if (loaderPoolArenasActive()) {
		const arena_data_t *src = loaderPoolGetArena(arena_index);
		if (src) {
			s_Arenas[arena_index] = *src;
			return &s_Arenas[arena_index];
		}
		/* Loader has no record for this slot. Fall through to the
		 * existing mirror (populated from the catalog row at init)
		 * so the manager iterator still produces consistent values. */
	}
#endif

	return &s_Arenas[arena_index];
}

void catalogManagerArenaInit(void)
{
	s32 i;

	/* Zero-init the pool. Slots that are not covered by any
	 * ASSET_ARENA catalog row stay zero; consumers must filter by
	 * arena->catalog_id[0] != '\0' if they need to skip empty slots. */
	for (i = 0; i < CATALOG_MGR_ARENA_COUNT; i++) {
		memset(&s_Arenas[i], 0, sizeof(arena_data_t));
		s_Arenas[i].arena_index = (s16)i;
	}

	/* Walk every ASSET_ARENA catalog row, populate the matching
	 * pool slot. assetCatalogIterateByType is a no-op on pd-server
	 * (no entries registered), so the server-side pool stays
	 * zero-initialised -- matches the heads / bodies pattern. */
	assetCatalogIterateByType(ASSET_ARENA, s_initCollectCb, NULL);

	s_ArenasInited = 1;
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.ARENA.LOAD: init complete count=%d (parity-period bridge)",
		CATALOG_MGR_ARENA_COUNT);
}

s32 catalogManagerArenaCount(void)
{
	return CATALOG_MGR_ARENA_COUNT;
}

const arena_data_t *catalogManagerGetArenaByIndex(s32 arena_index)
{
	if (arena_index < 0 || arena_index >= CATALOG_MGR_ARENA_COUNT) {
		return NULL;
	}
	return s_get(arena_index);
}

const arena_data_t *catalogManagerGetArenaAt(s32 iter_index)
{
	if (iter_index < 0 || iter_index >= CATALOG_MGR_ARENA_COUNT) {
		return NULL;
	}
	return s_get(iter_index);
}

const arena_data_t *catalogManagerGetArenaById(const char *catalog_id)
{
	const asset_entry_t *e;
	s32 arena_index;

	if (catalog_id == NULL || catalog_id[0] == '\0') {
		return NULL;
	}

	e = assetCatalogResolve(catalog_id);
	if (e == NULL || e->type != ASSET_ARENA) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.ARENA.MISS: catalog_id=\"%s\" not registered",
			catalog_id);
		return NULL;
	}

	/* runtime_index for ASSET_ARENA entries is the position in
	 * g_MpArenas[] (set in assetcatalog_base.c arena registration loop). */
	arena_index = e->runtime_index;
	if (!catalogMgrArenaIsInRangePure(arena_index)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.ARENA.MISS: catalog_id=\"%s\" runtime_index=%d invalid",
			catalog_id, arena_index);
		return NULL;
	}
	return catalogManagerGetArenaByIndex(arena_index);
}

const arena_data_t *catalogManagerGetArenaByStagenum(s16 stagenum)
{
	s32 i;

	if (!s_ArenasInited) {
		catalogManagerArenaInit();
	}

	for (i = 0; i < CATALOG_MGR_ARENA_COUNT; i++) {
		const arena_data_t *a = &s_Arenas[i];
		/* Skip empty slots (no ASSET_ARENA row registered for this index). */
		if (a->catalog_id[0] == '\0') {
			continue;
		}
		if (a->stagenum == stagenum) {
			return a;
		}
	}
	return NULL;
}

/* Registration / unregistration. F12 wires the loader_pool through
 * RegisterArena. Until then these update the live pool and log the
 * override channel. */

void catalogManagerRegisterArena(const char *id, const arena_data_t *data)
{
	const asset_entry_t *e;
	s32 arena_index;

	if (id == NULL || data == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_ARENA) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.ARENA.MISS: register id=\"%s\" not in catalog",
			id);
		return;
	}
	arena_index = e->runtime_index;
	if (!catalogMgrArenaIsInRangePure(arena_index)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.ARENA.MISS: register id=\"%s\" arena_index=%d invalid",
			id, arena_index);
		return;
	}
	memcpy(&s_Arenas[arena_index], data, sizeof(arena_data_t));
	s_Arenas[arena_index].arena_index = (s16)arena_index;
	strncpy(s_Arenas[arena_index].catalog_id, id,
		sizeof(s_Arenas[arena_index].catalog_id) - 1);
	s_Arenas[arena_index].catalog_id[sizeof(s_Arenas[arena_index].catalog_id) - 1] = '\0';
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.ARENA.OVERRIDE: id=\"%s\" arena_index=%d",
		id, arena_index);
}

void catalogManagerUnregisterArena(const char *id)
{
	const asset_entry_t *e;
	s32 arena_index;

	if (id == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_ARENA) {
		return;
	}
	arena_index = e->runtime_index;
	if (!catalogMgrArenaIsInRangePure(arena_index)) {
		return;
	}
	/* Revert to the catalog-row-derived values (F1 parity). F12 reverts
	 * to the loader_pool entry. */
	memset(&s_Arenas[arena_index], 0, sizeof(arena_data_t));
	s_Arenas[arena_index].arena_index = (s16)arena_index;
	s_initCollectCb(e, NULL);
}

void catalogManagerArenaShutdown(void)
{
	s_ArenasInited = 0;
}
