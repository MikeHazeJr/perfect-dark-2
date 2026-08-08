/*
 * port/src/catalog_mgr_heads.c -- Catalog Gate 3 F1: Catalog Manager for
 * character heads.
 *
 * See port/include/catalog_mgr_heads.h for the public contract and
 * context/audits/catalog-gate3-heads-data-2026-05-01.md for the design
 * rationale.
 *
 * Phase 2 (F1-F10): this manager is a thin pass-through router over the
 * legacy g_HeadsAndBodies[] static array for HEAD slots. F2 wires the
 * catalogGetHeadX accessors (assetcatalog_api.c) through this manager;
 * F3 / F4 migrate the modeldef cache; F6 retires the random-gender
 * static arrays. F11-F13 replace the legacy backing table with manager-
 * owned data sourced from base/headsthe per-asset envelope.
 *
 * Pure validators live in port/src/catalog_mgr_heads_pure.c and are
 * pinned by tests/test_catalog_mgr_heads_api.cpp.
 */

#include <ultra64.h>
#include <stddef.h>
#include <string.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "system.h"
#if !defined(PD_SERVER)
#include "lib/rng.h"  /* rngRandom: client only, server has no rng_c.c */
#include "game/modeldef.h"  /* modeldefLoadToNewFromHandle */
#endif
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetprovider.h"
#include "asset_source_debug.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_heads_pure.h"
#include "loader_pool.h"  /* Catalog Gate 3 F12: pool-backed source when active */

/* c3844 Gate 2: keep the pure bound constants in lockstep with the manager
 * constants at compile time (see catalog_mgr_bodies.c). */
_Static_assert(CATALOG_MGR_HEAD_COUNT_PURE == CATALOG_MGR_HEAD_COUNT,
	"head base count drift between pure and manager headers");
_Static_assert(CATALOG_MGR_HEAD_CUSTOM_COUNT_PURE == CATALOG_MGR_HEAD_CUSTOM_COUNT,
	"head custom count drift between pure and manager headers");
_Static_assert(CATALOG_MGR_HEAD_TOTAL_PURE == CATALOG_MGR_HEAD_TOTAL,
	"head total drift between pure and manager headers");
/* BYOR completion (2026-05-03): manager seeds from authoring table
 * instead of g_HeadsAndBodies[]. */
#include "headdata_authored.h"

/* F1 backing pool: parallel mirror that the manager owns. Populated
 * at init from g_HeadData[] (BYOR completion 2026-05-03; was
 * g_HeadsAndBodies[] pre-pivot). Loader pool overrides on the F12 path. */
/* c3844 Gate 2: sized to TOTAL so a catalog-owned custom head slot
 * [152, TOTAL) is valid storage indexable directly by the render path. */
static head_data_t s_Heads[CATALOG_MGR_HEAD_TOTAL];
static s32 s_HeadsInited = 0;

static void s_populateFromAuthored(s32 headnum)
{
	const head_authored_record_t *src;
	head_data_t *dst;

	if (headnum < 0 || headnum >= CATALOG_MGR_HEAD_TOTAL) {
		return;
	}
	src = headDataLookupByHeadnum(headnum);
	dst = &s_Heads[headnum];
	dst->headnum = (s16)headnum;
	dst->catalog_id[0] = '\0';
	if (!src) {
		/* Slot has no authored head (was a body slot or sentinel). Leave
		 * the rest of dst zeroed (already memset in init). */
		dst->ismale = 0;
		dst->unk00_01 = 0;
		dst->type = 0;
		dst->height = 0;
		dst->filenum = 0;
		dst->scale = 0.0f;
		dst->animscale = 0.0f;
		return;
	}
	dst->ismale   = src->ismale;
	dst->unk00_01 = src->unk00_01;
	dst->type     = src->type;
	dst->height   = src->height;
	dst->filenum  = src->filenum;
	dst->scale    = src->scale;
	dst->animscale= src->animscale;
	/* Modeldef cache stays manager-owned -- no copy from src. */
}

static const head_data_t *s_get(s32 headnum)
{
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		return NULL;
	}
	if (!s_HeadsInited) {
		catalogManagerHeadInit();
	}
#if !defined(PD_SERVER)
	/* Catalog Gate 3 F12: when the loader is active, the loader_pool
	 * is the source of truth.  Copy the loader-owned record into the
	 * manager slot (preserving the modeldef cache pointer) so all
	 * accessors continue to read from s_Heads[].  F13 retires the
	 * legacy mirror entirely; for now we keep both paths so the
	 * parity bridge can be exercised. */
	if (loaderPoolHeadsActive()) {
		const head_data_t *src = loaderPoolGetHead(headnum);
		if (src) {
			struct modeldef *md = s_Heads[headnum].modeldef;
			s_Heads[headnum] = *src;
			s_Heads[headnum].modeldef = md;
			return &s_Heads[headnum];
		}
		if (headnum >= CATALOG_MGR_HEAD_CUSTOM_START) {
			const head_data_t *custom = &s_Heads[headnum];
			if (custom->filenum != 0 || custom->catalog_id[0] != '\0') {
				return custom;
			}
			return NULL;
		}
		/* Loader has no record for this slot (e.g. body slot or sentinel
		 * entry that does not appear in the heads envelope). Fall through to
		 * the legacy mirror so manager iteration over the full 152-slot
		 * range still produces consistent values for non-head slots. */
	}
#endif
	if (headnum >= CATALOG_MGR_HEAD_CUSTOM_START) {
		const head_data_t *custom = &s_Heads[headnum];
		if (custom->filenum != 0 || custom->catalog_id[0] != '\0') {
			return custom;
		}
		return NULL;
	}
	/* Parity-period fallback: re-read from the legacy table so any
	 * out-of-band mutation (e.g. lazy modeldef cache writes via the
	 * legacy catalogGetHeadModeldef path that pre-dated F3) stays
	 * observable. F3+ owns the modeldef cache; the rest of the fields
	 * are read-only during the parity period. */
	s_populateFromAuthored(headnum);
	return &s_Heads[headnum];
}

void catalogManagerHeadInit(void)
{
	s32 i;

	/* c3844 Gate 2: zero-init across the full TOTAL range so the private
	 * custom head slots [152, TOTAL) start clean. */
	for (i = 0; i < CATALOG_MGR_HEAD_TOTAL; i++) {
		memset(&s_Heads[i], 0, sizeof(head_data_t));
		s_Heads[i].headnum = (s16)i;
	}
	for (i = 0; i < CATALOG_MGR_HEAD_TOTAL; i++) {
		s_populateFromAuthored(i);
	}
	s_HeadsInited = 1;
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.HEAD.LOAD: init complete count=%d (parity-period bridge)",
		CATALOG_MGR_HEAD_COUNT);
}

s32 catalogManagerHeadCount(void)
{
	return CATALOG_MGR_HEAD_COUNT;
}

const head_data_t *catalogManagerGetHeadByIndex(s32 headnum)
{
	/* Negative is silent (matches HEAD_RANDOM_GENDER + uninitialised-
	 * head-slot legitimate sentinels). Out-of-positive is silent too
	 * during F1 because the legacy accessors that we'll proxy did not
	 * log on out-of-range; F2 may reconsider per-callsite. */
	/* c3844 Gate 2: render-path lookup accepts the custom range too. */
	if (headnum < 0 || headnum >= CATALOG_MGR_HEAD_TOTAL) {
		return NULL;
	}
	if (headnum == CATALOG_MGR_HEAD_RANDOM_GENDER_PURE) {
		return NULL;
	}
	return s_get(headnum);
}

const head_data_t *catalogManagerGetHeadAt(s32 iter_index)
{
	if (iter_index < 0 || iter_index >= CATALOG_MGR_HEAD_COUNT) {
		return NULL;
	}
	return s_get(iter_index);
}

const head_data_t *catalogManagerGetHeadById(const char *catalog_id)
{
	const asset_entry_t *e;
	s32 headnum;

	if (catalog_id == NULL || catalog_id[0] == '\0') {
		return NULL;
	}

	e = assetCatalogResolve(catalog_id);
	if (e == NULL || e->type != ASSET_HEAD) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.HEAD.MISS: catalog_id=\"%s\" not registered",
			catalog_id);
		return NULL;
	}

	/* runtime_index for ASSET_HEAD entries is the g_HeadsAndBodies[]
	 * index (set in assetcatalog_base.c::registerBaseHeads). */
	headnum = e->runtime_index;
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.HEAD.MISS: catalog_id=\"%s\" runtime_index=%d invalid",
			catalog_id, headnum);
		return NULL;
	}
	return catalogManagerGetHeadByIndex(headnum);
}

/* ========================================================================
 * Modeldef cache. F1 ships pass-throughs that delegate to the legacy
 * g_HeadsAndBodies[].modeldef slot; F3/F4 migrate the cache to the
 * manager pool slot.
 * ======================================================================== */

#if !defined(PD_SERVER)
static s32 catalogManagerHeadModeldefPassesSourceOnlyCheck(s32 headnum,
	const char *id, asset_data_handle_t handle)
{
	assetSourceDebugFatalHandleFallback(ASSET_HEAD,
		"head manager modeldef fallback", id, handle);

	if (!assetSourceDebugHandleRequiresPublicFileSource(ASSET_HEAD, handle)) {
		return 1;
	}

	sysLogPrintf(LOG_WARNING,
		"CATALOG.MGR.HEAD.MISS: source-only head '%s' headnum=%d refused non-public model handle",
		id ? id : "(null)", headnum);
	return 0;
}
#endif

struct modeldef *catalogManagerGetHeadModeldef(s32 headnum)
{
#if defined(PD_SERVER)
	(void)headnum;
	/* Server has no model data; modeldef calls always return NULL. */
	return NULL;
#else
	/* F3: manager owns the modeldef cache slot.  Lazy-load on first
	 * call into s_Heads[h].modeldef, return the cached pointer
	 * thereafter.  The legacy g_HeadsAndBodies[].modeldef slot is
	 * orphaned for HEAD entries; bodies session retires it when the
	 * bodies migration moves the bodies-side cache too. */
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		return NULL;
	}
	if (!s_HeadsInited) {
		catalogManagerHeadInit();
	}
	if (!s_Heads[headnum].modeldef) {
		const char *id = catalogHeadIdByHeadnum(headnum);
		s32 filenum = catalogGetHeadFilenumByIndex(headnum);

		if (id && catalogLoadStageAsset(ASSET_HEAD, id)) {
			s_Heads[headnum].modeldef = catalogGetLoadedModeldef(id);
		}
		if (s_Heads[headnum].modeldef) {
			return s_Heads[headnum].modeldef;
		}
		{
			const asset_entry_t *e = id ? assetCatalogResolve(id) : NULL;
			if (e && e->source.primary.provider == fileProvider()) {
				sysLogPrintf(LOG_WARNING,
					"CATALOG.MGR.HEAD.MISS: catalog_id=\"%s\" public source modeldef conversion failed",
					id ? id : "(null)");
				return NULL;
			}
		}

		{
			asset_data_handle_t handle = catalogGetHeadHandle(headnum);

			if (!catalogManagerHeadModeldefPassesSourceOnlyCheck(headnum, id,
					handle)) {
				return NULL;
			}

			s_Heads[headnum].modeldef = modeldefLoadToNewFromHandle(handle,
				(u16)filenum);
		}
	}
	return s_Heads[headnum].modeldef;
#endif
}

s32 catalogManagerHeadIsModeldefLoaded(s32 headnum)
{
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		return 0;
	}
	if (!s_HeadsInited) {
		return 0;
	}
	/* F3: probe the manager pool slot.  Replaces the legacy
	 * g_HeadsAndBodies[h].modeldef NULL pre-check pattern in
	 * src/game/body.c (F5 migrates the call site). */
	return s_Heads[headnum].modeldef != NULL ? 1 : 0;
}

void catalogManagerResetHeadModeldef(s32 headnum)
{
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		return;
	}
	if (!s_HeadsInited) {
		return;
	}
	/* F3: clear the manager pool slot. */
	s_Heads[headnum].modeldef = NULL;
}

void catalogManagerResetAllHeadModeldefs(void)
{
	/* F4 (decision I.4 Option B): iterate manager pool slots and clear
	 * the cache field. During the F1 parity period the cache lives on
	 * the legacy slot, so we delegate to catalogResetAllModeldefs
	 * (which itself walks g_HeadsAndBodies[].modeldef). After F3/F4
	 * the manager owns the cache. */
	s32 i;

	if (!s_HeadsInited) {
		return;
	}
	/* c3844 Gate 2: release custom-slot modeldefs too. */
	for (i = 0; i < CATALOG_MGR_HEAD_TOTAL; i++) {
		s_Heads[i].modeldef = NULL;
	}
	/* No legacy-walk delegate here -- catalogResetAllModeldefs() is
	 * the ENTRY point that calls us first, then continues its legacy
	 * walk (per I.4 Option B). Double-delegating would loop. */
}

/* ========================================================================
 * Random-gender pool helpers. F6 retires g_MpMaleHeads / g_MpFemaleHeads
 * by routing mpDefaultHeadForBody through these functions. The F1 build
 * implements the iteration so the test layer can pin the predicate from
 * day one.
 * ======================================================================== */

#if !defined(PD_SERVER)
static s32 s_pickRandomByGender(s32 want_male)
{
	s32 candidates[CATALOG_MGR_HEAD_COUNT];
	s32 count = 0;
	s32 i;

	if (!s_HeadsInited) {
		catalogManagerHeadInit();
	}

	for (i = 0; i < CATALOG_MGR_HEAD_COUNT; i++) {
		const head_data_t *h = &s_Heads[i];
		if (catalogMgrHeadIsGenderPoolEligiblePure(
				(s32)h->ismale, (s32)h->unk00_01, want_male)) {
			candidates[count++] = i;
		}
	}

	if (count == 0) {
		/* Empty pool fallback: the legacy code never had this case
		 * because g_MpMaleHeads / g_MpFemaleHeads were hand-curated
		 * and always non-empty. Return HEAD_RANDOM_GENDER as a safe
		 * "leave to body default" sentinel. */
		return CATALOG_MGR_HEAD_RANDOM_GENDER_PURE;
	}
	return candidates[rngRandom() % (u32)count];
}

s32 catalogManagerHeadPickRandomMale(void)
{
	return s_pickRandomByGender(1);
}

s32 catalogManagerHeadPickRandomFemale(void)
{
	return s_pickRandomByGender(0);
}
#else
/* Server has no rng_c.c; the random-gender pool is a client-only
 * concern. Stubs return HEAD_RANDOM_GENDER (sentinel "no head") so
 * any accidental server-side call is safe. */
s32 catalogManagerHeadPickRandomMale(void)
{
	return CATALOG_MGR_HEAD_RANDOM_GENDER_PURE;
}

s32 catalogManagerHeadPickRandomFemale(void)
{
	return CATALOG_MGR_HEAD_RANDOM_GENDER_PURE;
}
#endif

/* ========================================================================
 * Registration / unregistration. F12 wires the loader_pool through
 * RegisterHead. Until then these are no-ops with logging so the API is
 * stable for callers that don't care about the load source.
 * ======================================================================== */

void catalogManagerRegisterHead(const char *id, const head_data_t *data)
{
	const asset_entry_t *e;
	s32 headnum;

	if (id == NULL || data == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_HEAD) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.HEAD.MISS: register id=\"%s\" not in catalog",
			id);
		return;
	}
	headnum = e->runtime_index;
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.MGR.HEAD.MISS: register id=\"%s\" headnum=%d invalid",
			id, headnum);
		return;
	}
	if (!s_HeadsInited) {
		catalogManagerHeadInit();
	}
	memcpy(&s_Heads[headnum], data, sizeof(head_data_t));
	s_Heads[headnum].headnum = (s16)headnum;
	strncpy(s_Heads[headnum].catalog_id, id,
		sizeof(s_Heads[headnum].catalog_id) - 1);
	s_Heads[headnum].catalog_id[sizeof(s_Heads[headnum].catalog_id) - 1] = '\0';
	sysLogPrintf(LOG_NOTE,
		"CATALOG.MGR.HEAD.OVERRIDE: id=\"%s\" headnum=%d",
		id, headnum);
}

void catalogManagerUnregisterHead(const char *id)
{
	const asset_entry_t *e;
	s32 headnum;

	if (id == NULL) {
		return;
	}
	e = assetCatalogResolve(id);
	if (e == NULL || e->type != ASSET_HEAD) {
		return;
	}
	headnum = e->runtime_index;
	if (!catalogMgrHeadIsInRangePure(headnum)) {
		return;
	}
	/* Revert to legacy-table values (F1 parity). F12 reverts to base
	 * pool entry instead. */
	s_populateFromAuthored(headnum);
}

void catalogManagerHeadShutdown(void)
{
	s_HeadsInited = 0;
}
