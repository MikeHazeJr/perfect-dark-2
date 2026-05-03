/*
 * port/src/catalog_mgr_bodies.c -- Catalog Gate 3 Bodies F1: Catalog
 * Manager for character bodies.
 *
 * See port/include/catalog_mgr_bodies.h for the public contract and
 * context/audits/catalog-gate3-bodies-data-2026-05-02.md for the design
 * rationale.
 *
 * Phase 2 (F1-F11): this manager is a thin pass-through router over the
 * legacy g_HeadsAndBodies[] static array for BODY slots. F2 wires the
 * catalogGetBodyX accessors (assetcatalog_api.c) through this manager;
 * F3 / F4 migrate the modeldef cache and retire the legacy walk.
 * F11-F13 replace the legacy backing table with manager-owned data
 * sourced from base/bodiesthe per-asset envelope.
 *
 * Pure validators live in port/src/catalog_mgr_bodies_pure.c and are
 * pinned by tests/test_catalog_mgr_bodies_api.cpp.
 */

#include <ultra64.h>
#include <stddef.h>
#include <string.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "system.h"
#if !defined(PD_SERVER)
#include "game/modeldef.h"  /* modeldefLoadToNewFromHandle */
#endif
#include "assetcatalog.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_bodies_pure.h"
#include "loader_pool.h"  /* Catalog Gate 3 Bodies F12: pool-backed source when active */

extern struct headorbody g_HeadsAndBodies[];

/* F1 backing pool: a parallel mirror that the manager owns. Populated
 * lazily from g_HeadsAndBodies[] during F1-F11 (parity period); F12
 * switches to loader-owned source. */
static body_data_t s_Bodies[CATALOG_MGR_BODY_COUNT];
static s32 s_BodiesInited = 0;

static void s_populateFromLegacy(s32 bodynum)
{
	struct headorbody *src;
	body_data_t *dst;

	if (bodynum < 0 || bodynum >= CATALOG_MGR_BODY_COUNT) {
		return;
	}
	src = &g_HeadsAndBodies[bodynum];
	dst = &s_Bodies[bodynum];
	dst->bodynum = (s16)bodynum;
	dst->catalog_id[0] = '\0';
	dst->ismale = (u8)src->ismale;
	dst->unk00_01 = (u8)src->unk00_01;
	dst->canvaryheight = (u8)src->canvaryheight;
	dst->type = (u8)src->type;
	dst->height = (u16)src->height;
	dst->filenum = src->filenum;
	dst->scale = src->scale;
	dst->animscale = src->animscale;
	dst->handfilenum = src->handfilenum;
	/* F3: manager owns the modeldef cache slot.  Do NOT copy from
	 * src->modeldef -- the manager's lazy-load + cache lives on
	 * dst->modeldef from F3 onward. The legacy g_HeadsAndBodies[].modeldef
	 * slot is orphaned for BODY entries after F3. */
}

static const body_data_t *s_get(s32 bodynum)
{
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		return NULL;
	}
	if (!s_BodiesInited) {
		catalogManagerBodyInit();
	}
#if !defined(PD_SERVER)
	/* Catalog Gate 3 Bodies F12: when the loader is active, the
	 * loader_pool is the source of truth.  Copy the loader-owned
	 * record into the manager slot (preserving the modeldef cache
	 * pointer) so all accessors continue to read from s_Bodies[].
	 * F13 retires the legacy mirror entirely; for now we keep both
	 * paths so the parity bridge can be exercised. */
	if (loaderPoolBodiesActive()) {
		const body_data_t *src = loaderPoolGetBody(bodynum);
		if (src) {
			struct modeldef *md = s_Bodies[bodynum].modeldef;
			s_Bodies[bodynum] = *src;
			s_Bodies[bodynum].modeldef = md;
			return &s_Bodies[bodynum];
		}
		/* Loader has no record for this slot (head-only or sentinel).
		 * Fall through to the legacy mirror so manager iteration over
		 * the full 152-slot range still produces consistent values. */
	}
#endif
	/* Parity-period fallback: re-read from the legacy table so any
	 * out-of-band mutation stays observable. F3+ owns the modeldef
	 * cache; the rest of the fields are read-only during the parity
	 * period. */
	s_populateFromLegacy(bodynum);
	return &s_Bodies[bodynum];
}

void catalogManagerBodyInit(void)
{
	s32 i;

	for (i = 0; i < CATALOG_MGR_BODY_COUNT; i++) {
		memset(&s_Bodies[i], 0, sizeof(body_data_t));
		s_Bodies[i].bodynum = (s16)i;
	}
	for (i = 0; i < CATALOG_MGR_BODY_COUNT; i++) {
		s_populateFromLegacy(i);
	}
	s_BodiesInited = 1;
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.BODY.LOAD: init complete count=%d (parity-period bridge)",
		CATALOG_MGR_BODY_COUNT);
}

s32 catalogManagerBodyCount(void)
{
	return CATALOG_MGR_BODY_COUNT;
}

const body_data_t *catalogManagerGetBodyByIndex(s32 bodynum)
{
	if (bodynum < 0 || bodynum >= CATALOG_MGR_BODY_COUNT) {
		return NULL;
	}
	return s_get(bodynum);
}

const body_data_t *catalogManagerGetBodyAt(s32 iter_index)
{
	if (iter_index < 0 || iter_index >= CATALOG_MGR_BODY_COUNT) {
		return NULL;
	}
	return s_get(iter_index);
}

const body_data_t *catalogManagerGetBodyById(const char *catalog_id)
{
	const asset_entry_t *e;
	s32 bodynum;

	if (catalog_id == NULL || catalog_id[0] == '\0') {
		return NULL;
	}

	e = assetCatalogResolve(catalog_id);
	if (e == NULL || e->type != ASSET_BODY) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.BODY.MISS: catalog_id=\"%s\" not registered",
			catalog_id);
		return NULL;
	}

	/* runtime_index for ASSET_BODY entries is the g_HeadsAndBodies[]
	 * index (set in assetcatalog_base.c::registerBaseBodies). */
	bodynum = e->runtime_index;
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.BODY.MISS: catalog_id=\"%s\" runtime_index=%d invalid",
			catalog_id, bodynum);
		return NULL;
	}
	return catalogManagerGetBodyByIndex(bodynum);
}

/* ========================================================================
 * Modeldef cache. F3 owns the cache slot on the manager pool. The
 * legacy g_HeadsAndBodies[].modeldef slot is orphaned for BODY entries
 * after F3.
 * ======================================================================== */

struct modeldef *catalogManagerGetBodyModeldef(s32 bodynum)
{
#if defined(PD_SERVER)
	(void)bodynum;
	/* Server has no model data; modeldef calls always return NULL. */
	return NULL;
#else
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		return NULL;
	}
	if (!s_BodiesInited) {
		catalogManagerBodyInit();
	}
	if (!s_Bodies[bodynum].modeldef) {
		const char *id;
		const asset_entry_t *e;
		s32 fallback_filenum;

		id = catalogBodyIdByBodynum(bodynum);
		e = id ? assetCatalogResolve(id) : NULL;
		fallback_filenum = e ? e->source_filenum : -1;

		s_Bodies[bodynum].modeldef = modeldefLoadToNewFromHandle(
			catalogGetBodyHandle(bodynum),
			(u16)fallback_filenum);
	}
	return s_Bodies[bodynum].modeldef;
#endif
}

s32 catalogManagerBodyIsModeldefLoaded(s32 bodynum)
{
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		return 0;
	}
	if (!s_BodiesInited) {
		return 0;
	}
	return s_Bodies[bodynum].modeldef != NULL ? 1 : 0;
}

void catalogManagerResetBodyModeldef(s32 bodynum)
{
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		return;
	}
	if (!s_BodiesInited) {
		return;
	}
	s_Bodies[bodynum].modeldef = NULL;
}

void catalogManagerResetAllBodyModeldefs(void)
{
	s32 i;

	if (!s_BodiesInited) {
		return;
	}
	for (i = 0; i < CATALOG_MGR_BODY_COUNT; i++) {
		s_Bodies[i].modeldef = NULL;
	}
}

/* ========================================================================
 * Registration / unregistration. F12 wires the loader_pool through
 * RegisterBody. Until then these are no-ops with logging so the API is
 * stable for callers that don't care about the load source.
 * ======================================================================== */

void catalogManagerRegisterBody(const char *id, const body_data_t *data)
{
	const asset_entry_t *e;
	s32 bodynum;

	if (id == NULL || data == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_BODY) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.BODY.MISS: register id=\"%s\" not in catalog",
			id);
		return;
	}
	bodynum = e->runtime_index;
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.BODY.MISS: register id=\"%s\" bodynum=%d invalid",
			id, bodynum);
		return;
	}
	memcpy(&s_Bodies[bodynum], data, sizeof(body_data_t));
	s_Bodies[bodynum].bodynum = (s16)bodynum;
	strncpy(s_Bodies[bodynum].catalog_id, id,
		sizeof(s_Bodies[bodynum].catalog_id) - 1);
	s_Bodies[bodynum].catalog_id[sizeof(s_Bodies[bodynum].catalog_id) - 1] = '\0';
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.BODY.OVERRIDE: id=\"%s\" bodynum=%d",
		id, bodynum);
}

void catalogManagerUnregisterBody(const char *id)
{
	const asset_entry_t *e;
	s32 bodynum;

	if (id == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_BODY) {
		return;
	}
	bodynum = e->runtime_index;
	if (!catalogMgrBodyIsInRangePure(bodynum)) {
		return;
	}
	/* Revert to legacy-table values (F1 parity). F12 reverts to base
	 * pool entry instead. */
	s_populateFromLegacy(bodynum);
}

void catalogManagerBodyShutdown(void)
{
	s_BodiesInited = 0;
}
