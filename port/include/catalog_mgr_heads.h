#ifndef PD_CATALOG_MGR_HEADS_H
#define PD_CATALOG_MGR_HEADS_H

#include <PR/ultratypes.h>

/*
 * port/include/catalog_mgr_heads.h -- Catalog Gate 3 F1: Catalog Manager
 * for character heads.
 *
 * Background and design rationale: see
 *   context/audits/catalog-gate3-heads-data-2026-05-01.md
 *
 * Mirrors the F11-F13 weapons template (see catalog_mgr_weapons.h). Phase 2
 * (F1-F10) status: this manager is a thin pass-through router over the
 * legacy g_HeadsAndBodies[] static array for HEAD slots. F2 wires the
 * catalogGetHeadX(headnum) accessors in assetcatalog_api.c through the
 * manager. F3-F6 migrate the modeldef cache and the random-gender pool.
 * F11-F13 replace the legacy backing table with manager-owned data sourced
 * from base/heads.pdbase JSON.
 *
 * Logging: every miss path emits a CATALOG.MGR.HEAD.MISS: warning so
 * upstream callers can be diagnosed. Every override / load emits the
 * matching CATALOG.MGR.HEAD.* channel line.
 *
 * Heads have ZERO data-mutation sites outside the catalog API (compare
 * weapons EYESPY name swap and currentPlayerSetWeaponPos -- neither has a
 * heads analogue). The only stateful field is the lazy modeldef cache
 * which the manager owns post-migration.
 *
 * Integrated-head invariant (S593g body.c warning gate at lines 413-414):
 * read-only for this migration. Bodies-side data; bodies session must
 * preserve.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct modeldef;

/* Range bound for headnum. Equals ARRAYCOUNT(g_HeadsAndBodies) = 152.
 * The legacy g_HeadsAndBodies[] is sized identically
 * (src/include/data.h:390). HEAD slots are scattered through the array
 * (entries with unk00_01 == 1 are standalone head models); BODY slots
 * have unk00_01 == 0. The manager pool is sized to cover the full range
 * so headnum can index it directly without an mp_idx remap. */
#define CATALOG_MGR_HEAD_COUNT 152

/* Heads-only typed payload (per audit Section B.2 / decision I.1).
 *
 * Mirrors the head-relevant subset of struct headorbody. Drops bodies-side
 * fields (canvaryheight, handfilenum). Bitfields are unpacked into byte /
 * short fields for clarity; ~5 bytes overhead per head, 152 heads total
 * = ~760 bytes pool overhead. Trivial.
 *
 * For BODY slots in the underlying array (unk00_01 == 0), the manager
 * pool entry is zero-initialised and never returned via the public
 * accessors -- a body-slot lookup hits a NULL body_canon at the catalog
 * layer first, so the manager never sees a body-only headnum.
 */
typedef struct head_data {
    /* Identity */
    s16  headnum;             /* g_HeadsAndBodies[] index back-ref */
    char catalog_id[64];      /* "base:head_carrington" / "base:sp_head_67" */

    /* Heads-only fields, bitfields unpacked */
    u8  ismale;               /* 0 = female, 1 = male */
    u8  unk00_01;             /* 1 = standalone head model (canonical heads); 0 = body-slot or sentinel */
    u8  type;                 /* HEADBODYTYPE_* */
    u16 height;               /* head height field used by player vv_headheight + Mr Blonde clamp */
    u16 filenum;              /* CHEAD_* ROM file ID */
    f32 scale;                /* head scale */
    f32 animscale;            /* per-head animation rate scaling */

    /* Manager-owned lazy modeldef cache. Populated on first
     * catalogManagerGetHeadModeldef call; reset by
     * catalogManagerResetHeadModeldef / *ResetAllHeadModeldefs. */
    struct modeldef *modeldef;
} head_data_t;

/* ========================================================================
 * Index-based hot-path accessor. O(1).
 * Returns NULL on out-of-range (loud miss log) or empty slot (silent NULL,
 * matches the bodies-side / non-head-slot case). The negative-input case
 * is silent (legitimate "no head" sentinel for HEAD_RANDOM_GENDER and
 * uninitialised head fields).
 * ======================================================================== */
const head_data_t *catalogManagerGetHeadByIndex(s32 headnum);

/* String-id accessor. Boundary use only; not a hot path.
 * Resolves catalog_id via assetCatalogResolve, expects ASSET_HEAD,
 * dereferences runtime_index back to the same head entry as
 * catalogManagerGetHeadByIndex. */
const head_data_t *catalogManagerGetHeadById(const char *catalog_id);

/* Iterator support: count of slots and accessor by 0-based iter index.
 * iter_index >= count returns NULL. The iterator surfaces ALL slots
 * including non-head ones (unk00_01 == 0); consumers must filter by
 * head_data->unk00_01 == 1 to see only standalone head models. */
s32 catalogManagerHeadCount(void);
const head_data_t *catalogManagerGetHeadAt(s32 iter_index);

/* Modeldef cache management.
 *
 * catalogManagerGetHeadModeldef -- lazy-load on first call; returns
 *   the cached pointer thereafter. Returns NULL for out-of-range or
 *   for HEAD_RANDOM_GENDER. Replaces catalogGetHeadModeldef once F3
 *   migrates that accessor.
 *
 * catalogManagerHeadIsModeldefLoaded -- predicate replacing the
 *   bondmove path's `g_HeadsAndBodies[headnum].modeldef == NULL` pre-
 *   check. Returns 0 if out-of-range or cache is empty; 1 if a model
 *   pointer is cached.
 *
 * catalogManagerResetHeadModeldef -- clears the cache for one slot.
 *   Routes to the legacy cache slot when the loader bridge is inactive
 *   (parity period); to the manager pool slot when active.
 *
 * catalogManagerResetAllHeadModeldefs -- clears every head slot's
 *   cache. bodiesReset calls this alongside the legacy walk.
 */
struct modeldef *catalogManagerGetHeadModeldef(s32 headnum);
s32 catalogManagerHeadIsModeldefLoaded(s32 headnum);
void catalogManagerResetHeadModeldef(s32 headnum);
void catalogManagerResetAllHeadModeldefs(void);

/* Random-gender pool helpers (decision I.2 -- replaces g_MpMaleHeads /
 * g_MpFemaleHeads static arrays).
 *
 * Iterates s_Heads[] filtered by `ismale && unk00_01` (male) or
 * `!ismale && unk00_01` (female). Only canonical head entries (unk00_01==1)
 * are eligible -- body slots with random gender flags are not in the pool.
 *
 * Returns a g_HeadsAndBodies[] index, or HEAD_RANDOM_GENDER (1000) if the
 * matching pool is empty (graceful fallback so callers never get -1 mid
 * resolution). */
s32 catalogManagerHeadPickRandomMale(void);
s32 catalogManagerHeadPickRandomFemale(void);

/* Lifecycle. */
void catalogManagerHeadInit(void);
void catalogManagerRegisterHead(const char *id, const head_data_t *data);
void catalogManagerUnregisterHead(const char *id);
void catalogManagerHeadShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_MGR_HEADS_H */
