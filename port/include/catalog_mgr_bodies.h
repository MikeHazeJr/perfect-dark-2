#ifndef PD_CATALOG_MGR_BODIES_H
#define PD_CATALOG_MGR_BODIES_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_bodies.h -- Catalog Gate 3 Bodies F1: Catalog
 * Manager for character bodies.
 *
 * Background and design rationale: see
 *   context/audits/catalog-gate3-bodies-data-2026-05-02.md
 *
 * Mirrors the validated heads template (catalog_mgr_heads.h) and the
 * F11-F13 weapons template (catalog_mgr_weapons.h). Phase 2 (F1-F11)
 * status: this manager is a thin pass-through router over the legacy
 * g_HeadsAndBodies[] static array for BODY slots. F2 wires the
 * catalogGetBodyX(bodynum) accessors in assetcatalog_api.c through the
 * manager. F3-F4 migrate the modeldef cache and retire the legacy
 * cache walk. F11-F13 replace the legacy backing table with manager-
 * owned data sourced from base/bodies.pdbase JSON.
 *
 * Logging: every miss path emits a CATALOG.MGR.BODY.MISS: warning so
 * upstream callers can be diagnosed. Every override / load emits the
 * matching CATALOG.MGR.BODY.* channel line.
 *
 * Bodies-specific carryover from heads: bodies preserve `canvaryheight`
 * and `handfilenum` fields (heads dropped both per heads I.1). The
 * integrated-head invariant (unk00_01 == 1 for Skedar / Dr Caroll /
 * EyeSpy) is the body-side semantic of the same bit head slots use to
 * signal "standalone head model"; the manager surfaces it through
 * catalogManagerGetBodyByIndex(...)->unk00_01 so the S593g bodyAllocate
 * Model warning gate (body.c:417) keeps suppressing for those bodies.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct modeldef;

/* Range bound for bodynum. Equals ARRAYCOUNT(g_HeadsAndBodies) = 152.
 * The legacy g_HeadsAndBodies[] is sized identically
 * (src/include/data.h:390). BODY slots are scattered through the array
 * (entries with unk00_01 == 0 are normal body models; unk00_01 == 1
 * with a body-class filenum are integrated-head bodies).  The manager
 * pool is sized to cover the full range so bodynum can index it
 * directly without an mp_idx remap. */
#define CATALOG_MGR_BODY_COUNT 152

/* Bodies-only typed payload (per audit Section B.1).
 *
 * Mirrors the body-relevant subset of struct headorbody. Preserves
 * `canvaryheight` and `handfilenum` (BODY-only fields heads dropped).
 * Bitfields are unpacked into byte / short fields for clarity; ~32 bytes
 * per body, 152 bodies total = ~5 KB pool overhead. Trivial.
 *
 * For HEAD slots in the underlying array (unk00_01 == 1 + head-class
 * filenum), the manager pool entry is zero-initialised and never
 * returned via the public accessors (the catalog ASSET_BODY layer never
 * yields a head-only slot, so the manager never sees a head bodynum).
 */
typedef struct body_data {
    /* Identity */
    s16  bodynum;             /* g_HeadsAndBodies[] index back-ref */
    char catalog_id[64];      /* "base:dark_combat" / "base:sp_body_67" */

    /* Body-relevant fields, bitfields unpacked */
    u8  ismale;               /* 0 = female, 1 = male */
    u8  unk00_01;             /* 1 = integrated-head body (Skedar/Dr Caroll/EyeSpy); 0 = normal body */
    u8  canvaryheight;        /* 1 = per-chr height variance (Skedar) */
    u8  type;                 /* HEADBODYTYPE_* */
    u16 height;               /* body height; bot.c speed scaling source */
    u16 filenum;              /* CBODY_* ROM file ID */
    f32 scale;                /* body scale; chr->model->scale source */
    f32 animscale;            /* per-body animation rate scaling */
    u16 handfilenum;          /* first-person hand model file ID; B-275 dependency */

    /* Manager-owned lazy modeldef cache. Populated on first
     * catalogManagerGetBodyModeldef call; reset by
     * catalogManagerResetBodyModeldef / *ResetAllBodyModeldefs. */
    struct modeldef *modeldef;
} body_data_t;

/* ========================================================================
 * Index-based hot-path accessor. O(1).
 * Returns NULL on out-of-range (loud miss log) or empty slot (silent NULL,
 * matches the heads-side / non-body-slot case).
 * ======================================================================== */
const body_data_t *catalogManagerGetBodyByIndex(s32 bodynum);

/* String-id accessor. Boundary use only; not a hot path.
 * Resolves catalog_id via assetCatalogResolve, expects ASSET_BODY,
 * dereferences runtime_index back to the same body entry as
 * catalogManagerGetBodyByIndex. */
const body_data_t *catalogManagerGetBodyById(const char *catalog_id);

/* Iterator support: count of slots and accessor by 0-based iter index.
 * iter_index >= count returns NULL. The iterator surfaces ALL slots
 * including non-body ones (head slots); consumers must filter by
 * body_data->filenum != 0 (skip sentinel) and by reading the catalog
 * row's type (ASSET_BODY) to see only body models. */
s32 catalogManagerBodyCount(void);
const body_data_t *catalogManagerGetBodyAt(s32 iter_index);

/* Modeldef cache management.
 *
 * catalogManagerGetBodyModeldef -- lazy-load on first call; returns
 *   the cached pointer thereafter. Returns NULL for out-of-range.
 *   Replaces catalogGetBodyModeldef once F3 migrates that accessor.
 *
 * catalogManagerBodyIsModeldefLoaded -- predicate replacing the legacy
 *   `g_HeadsAndBodies[bodynum].modeldef == NULL` pre-check pattern.
 *   Returns 0 if out-of-range or cache is empty; 1 if a model pointer
 *   is cached.
 *
 * catalogManagerResetBodyModeldef -- clears the cache for one slot.
 *
 * catalogManagerResetAllBodyModeldefs -- clears every body slot's
 *   cache. catalogResetAllModeldefs (assetcatalog_api.c) calls this
 *   alongside the heads counterpart.
 */
struct modeldef *catalogManagerGetBodyModeldef(s32 bodynum);
s32 catalogManagerBodyIsModeldefLoaded(s32 bodynum);
void catalogManagerResetBodyModeldef(s32 bodynum);
void catalogManagerResetAllBodyModeldefs(void);

/* Lifecycle. */
void catalogManagerBodyInit(void);
void catalogManagerRegisterBody(const char *id, const body_data_t *data);
void catalogManagerUnregisterBody(const char *id);
void catalogManagerBodyShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_BODIES_H */
