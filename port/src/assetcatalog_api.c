/**
 * assetcatalog_api.c -- SA-2: Modular catalog API layer
 *
 * Per-type resolution functions for the asset catalog.
 * Provides a single interception point for all asset lookups:
 * mod override injection, validation, session ID lookup, and wire marshaling.
 *
 * All catalogResolveX() functions return 1 on success, 0 on failure.
 * On failure, *out is zeroed. Callers MUST check the return value.
 * Failure is a hard error: logged at [CATALOG-ERROR] level.
 *
 * Resolution priority:
 *   catalogResolveX(id, out)          -- by catalog string ID (O(1) hash lookup)
 *   catalogResolveXBySession(sid, out) -- by session wire ID (O(1), match lifetime)
 *   catalogResolveByNetHash(hash)      -- by CRC32 net_hash (O(n), use sparingly)
 *
 * Wire helpers:
 *   catalogWriteAssetRef() / catalogReadAssetRef() are the ONLY functions
 *   that may serialize/deserialize asset references on the wire.
 *   They write/read a 2-byte session ID.
 *
 * Design doc: context/designs/session-catalog-and-modular-api.md §4
 */

#include <string.h>
#include <stdio.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "data.h"
#include "assetcatalog.h"
#include "assetprovider_internal.h"
#include "modelcatalog.h"
#include "net/sessioncatalog.h"
#include "net/netbuf.h"
#include "modmgr.h"
#if !defined(PD_SERVER)
#include "game/modeldef.h"
#endif

/* -------------------------------------------------------------------------
 * Internal fill helpers -- populate result struct from a resolved entry.
 * Variables declared at top of scope (C89 style).
 * ------------------------------------------------------------------------- */

static void s_fillBodyResult(const asset_entry_t *e, catalog_body_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry        = e;
    out->filenum      = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->model_scale  = e->model_scale;
    out->display_name = e->id;
    out->net_hash     = e->net_hash;
    out->session_id   = sessionCatalogLookupWireId(e->id);
}

static void s_fillHeadResult(const asset_entry_t *e, catalog_head_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry        = e;
    out->filenum      = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->model_scale  = e->model_scale;
    out->display_name = e->id;
    out->net_hash     = e->net_hash;
    out->session_id   = sessionCatalogLookupWireId(e->id);
}

static void s_fillStageResult(const asset_entry_t *e, catalog_stage_result_t *out)
{
    s32 idx;

    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->stagenum   = e->ext.map.stagenum;
    out->net_hash   = e->net_hash;
    out->session_id = sessionCatalogLookupWireId(e->id);

    idx = e->runtime_index;
    if (g_Stages != NULL && idx >= 0) {
        out->bgfileid      = (s32)g_Stages[idx].bgfileid;
        out->padsfileid    = (s32)g_Stages[idx].padsfileid;
        out->setupfileid   = (s32)g_Stages[idx].setupfileid;
        out->mpsetupfileid = (s32)g_Stages[idx].mpsetupfileid;
        out->tilefileid    = (s32)g_Stages[idx].tilefileid;
        /* Phase 4: populate handles internally so callers never need
         * romProviderHandle(fileid) outside the catalog/provider layer. */
        if (out->bgfileid      > 0) out->bg_handle      = romProviderHandle(out->bgfileid);
        if (out->padsfileid    > 0) out->pads_handle    = romProviderHandle(out->padsfileid);
        if (out->setupfileid   > 0) out->setup_handle   = romProviderHandle(out->setupfileid);
        if (out->mpsetupfileid > 0) out->mpsetup_handle = romProviderHandle(out->mpsetupfileid);
        if (out->tilefileid    > 0) out->tile_handle    = romProviderHandle(out->tilefileid);
    } else {
        /* Server build: g_Stages is NULL; file IDs not available.
         * Handle fields remain null (zeroed by memset above). */
        out->bgfileid      = (e->source_filenum >= 0) ? e->source_filenum : -1;
        out->padsfileid    = -1;
        out->setupfileid   = -1;
        out->mpsetupfileid = -1;
        out->tilefileid    = -1;
    }
}

static void s_fillWeaponResult(const asset_entry_t *e, catalog_weapon_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->filenum    = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->weapon_num = e->ext.weapon.weapon_id;
    out->net_hash   = e->net_hash;
    out->session_id = sessionCatalogLookupWireId(e->id);
}

static void s_fillPropResult(const asset_entry_t *e, catalog_prop_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->filenum    = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->prop_type  = e->ext.prop.prop_type;
    out->net_hash   = e->net_hash;
    out->session_id = sessionCatalogLookupWireId(e->id);
}

static void s_fillAudioResult(const asset_entry_t *e, catalog_audio_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry     = e;
    out->sound_id  = e->ext.audio.sound_id;
    out->category  = e->ext.audio.category;
    out->file_path = e->ext.audio.file_path;
}

/* -------------------------------------------------------------------------
 * Resolution by catalog string ID
 * ------------------------------------------------------------------------- */

s32 catalogResolveBody(const char *id, catalog_body_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_BODY) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveBody: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillBodyResult(e, out);
    return 1;
}

s32 catalogResolveHead(const char *id, catalog_head_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_HEAD) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveHead: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillHeadResult(e, out);
    return 1;
}

s32 catalogResolveStage(const char *id, catalog_stage_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_MAP) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveStage: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillStageResult(e, out);
    return 1;
}

s32 catalogResolveWeapon(const char *id, catalog_weapon_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_WEAPON) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveWeapon: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillWeaponResult(e, out);
    return 1;
}

s32 catalogResolveProp(const char *id, catalog_prop_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_PROP) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveProp: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillPropResult(e, out);
    return 1;
}

s32 catalogResolveAudio(const char *id, catalog_audio_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_AUDIO) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveAudio: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillAudioResult(e, out);
    return 1;
}

/* -------------------------------------------------------------------------
 * Resolution by session wire ID
 *
 * Client: uses sessionCatalogLocalResolve() (O(1) translation table).
 * Server: falls back to sessionCatalogLookupEntry() + string resolve.
 * ------------------------------------------------------------------------- */

s32 catalogResolveBodyBySession(u16 session_id, catalog_body_result_t *out)
{
    const asset_entry_t *e;
    const session_catalog_entry_t *sc;

    memset(out, 0, sizeof(*out));
    e = sessionCatalogLocalResolve(session_id);
    if (!e) {
        sc = sessionCatalogLookupEntry(session_id);
        if (sc && sc->catalog_id[0]) {
            e = assetCatalogResolve(sc->catalog_id);
        }
    }
    if (!e || e->type != ASSET_BODY) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveBodyBySession: id %u not resolved or wrong type",
                     (unsigned)session_id);
        return 0;
    }
    s_fillBodyResult(e, out);
    out->session_id = session_id;
    return 1;
}

s32 catalogResolveHeadBySession(u16 session_id, catalog_head_result_t *out)
{
    const asset_entry_t *e;
    const session_catalog_entry_t *sc;

    memset(out, 0, sizeof(*out));
    e = sessionCatalogLocalResolve(session_id);
    if (!e) {
        sc = sessionCatalogLookupEntry(session_id);
        if (sc && sc->catalog_id[0]) {
            e = assetCatalogResolve(sc->catalog_id);
        }
    }
    if (!e || e->type != ASSET_HEAD) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveHeadBySession: id %u not resolved or wrong type",
                     (unsigned)session_id);
        return 0;
    }
    s_fillHeadResult(e, out);
    out->session_id = session_id;
    return 1;
}

s32 catalogResolveStageBySession(u16 session_id, catalog_stage_result_t *out)
{
    const asset_entry_t *e;
    const session_catalog_entry_t *sc;

    memset(out, 0, sizeof(*out));
    e = sessionCatalogLocalResolve(session_id);
    if (!e) {
        sc = sessionCatalogLookupEntry(session_id);
        if (sc && sc->catalog_id[0]) {
            e = assetCatalogResolve(sc->catalog_id);
        }
    }
    if (!e || (e->type != ASSET_MAP && e->type != ASSET_ARENA)) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveStageBySession: id %u not resolved or wrong type",
                     (unsigned)session_id);
        return 0;
    }
    s_fillStageResult(e, out);
    out->session_id = session_id;
    return 1;
}

s32 catalogResolveWeaponBySession(u16 session_id, catalog_weapon_result_t *out)
{
    const asset_entry_t *e;
    const session_catalog_entry_t *sc;

    memset(out, 0, sizeof(*out));
    e = sessionCatalogLocalResolve(session_id);
    if (!e) {
        sc = sessionCatalogLookupEntry(session_id);
        if (sc && sc->catalog_id[0]) {
            e = assetCatalogResolve(sc->catalog_id);
        }
    }
    if (!e || e->type != ASSET_WEAPON) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveWeaponBySession: id %u not resolved or wrong type",
                     (unsigned)session_id);
        return 0;
    }
    s_fillWeaponResult(e, out);
    out->session_id = session_id;
    return 1;
}

s32 catalogResolvePropBySession(u16 session_id, catalog_prop_result_t *out)
{
    const asset_entry_t *e;
    const session_catalog_entry_t *sc;

    memset(out, 0, sizeof(*out));
    e = sessionCatalogLocalResolve(session_id);
    if (!e) {
        sc = sessionCatalogLookupEntry(session_id);
        if (sc && sc->catalog_id[0]) {
            e = assetCatalogResolve(sc->catalog_id);
        }
    }
    if (!e || e->type != ASSET_PROP) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolvePropBySession: id %u not resolved or wrong type",
                     (unsigned)session_id);
        return 0;
    }
    s_fillPropResult(e, out);
    out->session_id = session_id;
    return 1;
}

/* -------------------------------------------------------------------------
 * Resolution by CRC32 net_hash
 * ------------------------------------------------------------------------- */

const asset_entry_t *catalogResolveByNetHash(u32 net_hash)
{
    return assetCatalogResolveByNetHash(net_hash);
}

/* =========================================================================
 * Phase 8: O(1) cached runtime lookups
 *
 * All integer↔catalog-ID resolution is pre-cached during init.  Zero O(n)
 * scans at runtime.  The caches are populated once by catalogBuildRuntimeCaches()
 * after base-game + mod registration completes.
 * ========================================================================= */

extern struct mpbody g_MpBodies[];
extern struct mphead g_MpHeads[];

#define MP_BODY_COUNT 63
#define MP_HEAD_COUNT 76
#define RT_CACHE_SIZE 1024

/* Forward caches: mp table position → catalog ID string */
static const char *s_MpBodyIdCache[MP_BODY_COUNT];
static const char *s_MpHeadIdCache[MP_HEAD_COUNT];

/* General cache: (type, runtime_index) → catalog ID string.
 * Indexed as s_RuntimeCache[type][runtime_index]. */
static const char *s_RuntimeCache[ASSET_TYPE_COUNT][RT_CACHE_SIZE];

static int s_RuntimeCacheBuilt = 0;

void catalogBuildRuntimeCaches(void)
{
    s32 i;
    const asset_entry_t *e;

    memset(s_MpBodyIdCache, 0, sizeof(s_MpBodyIdCache));
    memset(s_MpHeadIdCache, 0, sizeof(s_MpHeadIdCache));
    memset(s_RuntimeCache, 0, sizeof(s_RuntimeCache));

    /* Pass 1: build general runtime cache from all catalog entries */
    for (i = 0; ; i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) break;
        s32 t = (s32)e->type;
        s32 ri = e->runtime_index;
        if (t >= 0 && t < ASSET_TYPE_COUNT && ri >= 0 && ri < RT_CACHE_SIZE) {
            s_RuntimeCache[t][ri] = e->id;
        }
    }

    /* Pass 2: build mp body cache + populate mp_index on body entries */
    for (i = 0; i < MP_BODY_COUNT; i++) {
        s32 bodynum = (s32)g_MpBodies[i].bodynum;
        const char *cid = (bodynum >= 0 && bodynum < RT_CACHE_SIZE)
                          ? s_RuntimeCache[ASSET_BODY][bodynum] : NULL;
        s_MpBodyIdCache[i] = cid;
        if (cid) {
            asset_entry_t *ae = (asset_entry_t *)assetCatalogResolve(cid);
            if (ae) ae->mp_index = (s16)i;
        }
    }

    /* Pass 3: build mp head cache + populate mp_index on head entries */
    for (i = 0; i < MP_HEAD_COUNT; i++) {
        s32 headnum = (s32)g_MpHeads[i].headnum;
        const char *cid = (headnum >= 0 && headnum < RT_CACHE_SIZE)
                          ? s_RuntimeCache[ASSET_HEAD][headnum] : NULL;
        s_MpHeadIdCache[i] = cid;
        if (cid) {
            asset_entry_t *ae = (asset_entry_t *)assetCatalogResolve(cid);
            if (ae) ae->mp_index = (s16)i;
        }
    }

    s_RuntimeCacheBuilt = 1;
    sysLogPrintf(LOG_NOTE, "[CATALOG] Runtime caches built: %d body IDs, %d head IDs",
                 MP_BODY_COUNT, MP_HEAD_COUNT);
}

const char *catalogMpBodyId(s32 mp_idx)
{
    if (mp_idx < 0 || mp_idx >= MP_BODY_COUNT) return NULL;
    return s_MpBodyIdCache[mp_idx];
}

const char *catalogMpHeadId(s32 mp_idx)
{
    if (mp_idx < 0 || mp_idx >= MP_HEAD_COUNT) return NULL;
    return s_MpHeadIdCache[mp_idx];
}

const char *catalogIdByRuntime(asset_type_e type, s32 runtime_index)
{
    if ((s32)type < 0 || (s32)type >= ASSET_TYPE_COUNT) return NULL;
    if (runtime_index < 0 || runtime_index >= RT_CACHE_SIZE) return NULL;
    return s_RuntimeCache[(s32)type][runtime_index];
}

/* Body → default head catalog ID.  Reads ext.body.headnum from the body catalog
 * entry and resolves it to a HEAD catalog ID string via the runtime cache.
 * Returns NULL if body_id is unknown, wrong type, or headnum < 0. */
const char *catalogGetBodyDefaultHead(const char *body_id)
{
    const asset_entry_t *e;
    if (!body_id || !body_id[0]) return NULL;
    e = assetCatalogResolve(body_id);
    if (!e || e->type != ASSET_BODY) return NULL;
    if (e->ext.body.headnum < 0 || e->ext.body.headnum == HEAD_RANDOM_GENDER) return NULL;
    return catalogIdByRuntime(ASSET_HEAD, (s32)e->ext.body.headnum);
}

/* B-226: Body mp_index -> catalog display_name. Returns NULL if no override
 * was set at registration (caller should fall back to langbank). */
const char *catalogGetBodyDisplayName(s32 mpbodynum)
{
    const char *bid;
    const asset_entry_t *e;
    if (mpbodynum < 0 || mpbodynum >= MP_BODY_COUNT) return NULL;
    bid = s_MpBodyIdCache[mpbodynum];
    if (!bid) return NULL;
    e = assetCatalogResolve(bid);
    if (!e || e->type != ASSET_BODY) return NULL;
    if (!e->ext.body.display_name[0]) return NULL;
    return e->ext.body.display_name;
}

/* Body -> default head mpheadnum.  Uses cached mp_index on the head entry.
 * Returns -1 if the body is not found, has no default head, or the sentinel
 * value 1000 (random-gender head) is stored. */
s32 catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum)
{
    const char *bid;
    const asset_entry_t *e;
    const asset_entry_t *he;
    if (mpbodynum < 0 || mpbodynum >= MP_BODY_COUNT) return -1;
    bid = s_MpBodyIdCache[mpbodynum];
    if (!bid) return -1;
    e = assetCatalogResolve(bid);
    if (!e || e->type != ASSET_BODY) return -1;
    if (e->ext.body.headnum < 0) return -1;
    /* Look up the head entry via the runtime cache and read its mp_index */
    const char *hid = catalogIdByRuntime(ASSET_HEAD, (s32)e->ext.body.headnum);
    if (!hid) return -1;
    he = assetCatalogResolve(hid);
    if (!he || he->type != ASSET_HEAD) return -1;
    return (s32)he->mp_index;
}

/* -------------------------------------------------------------------------
 * Wire helpers
 * The ONLY functions that may serialize/deserialize asset references on wire.
 * ------------------------------------------------------------------------- */

void catalogWriteAssetRef(struct netbuf *buf, u16 session_id)
{
    netbufWriteU16(buf, session_id);
}

u16 catalogReadAssetRef(struct netbuf *buf)
{
    return netbufReadU16(buf);
}

/* -------------------------------------------------------------------------
 * SA-5 global failure state
 * Set by catalog helpers when a required asset is not found.
 * Callers should check g_CatalogFailure after load-path calls.
 * g_CatalogFailureMsg holds a human-readable description of the first miss.
 * ------------------------------------------------------------------------- */

s32  g_CatalogFailure = 0;
char g_CatalogFailureMsg[256] = {0};

/* -------------------------------------------------------------------------
 * SA-5a: Load-site helpers
 * Mod-override-aware filenum / scale resolution by runtime body/head index.
 * Used at model load call sites in body.c, player.c, menu.c, setup.c to
 * replace direct g_HeadsAndBodies[n].filenum / .scale accesses.
 * O(n) scan per call -- acceptable at load time (once per match start).
 * ------------------------------------------------------------------------- */

s32 catalogGetBodyFilenumByIndex(s32 bodynum)
{
    const char *id;
    catalog_body_result_t result;

    id = catalogIdByRuntime(ASSET_BODY, bodynum);
    if (id && catalogResolveBody(id, &result)) {
        sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, result.filenum);
        return result.filenum;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetBodyFilenumByIndex: bodynum=%d not in catalog", bodynum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: body bodynum=%d not found in catalog", bodynum);
    return 0;
}

s32 catalogGetHeadFilenumByIndex(s32 headnum)
{
    const char *id;
    catalog_head_result_t result;

    if (headnum == HEAD_RANDOM_GENDER) {
        return 0;
    }

    id = catalogIdByRuntime(ASSET_HEAD, headnum);
    if (id && catalogResolveHead(id, &result)) {
        sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, result.filenum);
        return result.filenum;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetHeadFilenumByIndex: headnum=%d not in catalog", headnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: head headnum=%d not found in catalog", headnum);
    return 0;
}

f32 catalogGetBodyScaleByIndex(s32 bodynum)
{
    const char *id;
    catalog_body_result_t result;

    id = catalogIdByRuntime(ASSET_BODY, bodynum);
    if (id && catalogResolveBody(id, &result)) {
        return result.model_scale;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetBodyScaleByIndex: bodynum=%d not in catalog", bodynum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: body scale bodynum=%d not found in catalog", bodynum);
    return 1.0f;
}

/* -------------------------------------------------------------------------
 * SA-5b: Stage load-site helper
 * Mod-override-aware stage file ID resolution by runtime stage array index.
 * Used at file load call sites in bg.c, tilesreset.c, setup.c to replace
 * direct g_Stages[stageindex].bgfileid / padsfileid / setupfileid /
 * mpsetupfileid / tilefileid accesses.
 * O(n) scan per call -- acceptable at load time (once per stage transition).
 * ------------------------------------------------------------------------- */

s32 catalogGetStageResultByIndex(s32 stageindex, catalog_stage_result_t *out)
{
    const char *id;

    memset(out, 0, sizeof(*out));
    id = catalogIdByRuntime(ASSET_MAP, stageindex);
    if (id && catalogResolveStage(id, out)) {
        return 1;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetStageResultByIndex: stageindex=%d not in catalog", stageindex);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: stage stageindex=%d not found in catalog", stageindex);
    return 0;
}

/* -------------------------------------------------------------------------
 * SA-5c: Prop model load-site helper
 * Mod-override-aware file ID resolution by runtime model array index (MODEL_*).
 * Used at model file load call sites in setupLoadModeldef(), player.c to
 * replace direct g_ModelStates[modelnum].fileid accesses.
 * Looks up ASSET_MODEL entries registered by assetCatalogRegisterBaseGameExtended().
 * O(n) scan per call -- acceptable at load time (result cached in modeldef).
 * ------------------------------------------------------------------------- */

s32 catalogGetPropFilenumByIndex(s32 propnum)
{
    const char *id;
    const asset_entry_t *e;

    id = catalogIdByRuntime(ASSET_MODEL, propnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, e->source_filenum);
            return e->source_filenum;
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetPropFilenumByIndex: propnum=%d not in catalog", propnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: prop model propnum=%d not found in catalog", propnum);
    return 0;
}

/* -------------------------------------------------------------------------
 * Phase 4: Handle-based load accessors
 * Return an asset_data_handle_t so callers never need romProviderHandle().
 * ------------------------------------------------------------------------- */

asset_data_handle_t catalogGetBodyHandle(s32 bodynum)
{
    const char *id;
    const asset_entry_t *e;
    asset_data_handle_t null_h;
    memset(&null_h, 0, sizeof(null_h));

    id = catalogIdByRuntime(ASSET_BODY, bodynum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            return catalogEffectiveHandle(e);
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetBodyHandle: bodynum=%d not in catalog", bodynum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: body bodynum=%d not found in catalog", bodynum);
    return null_h;
}

asset_data_handle_t catalogGetHeadHandle(s32 headnum)
{
    const char *id;
    const asset_entry_t *e;
    asset_data_handle_t null_h;
    memset(&null_h, 0, sizeof(null_h));

    if (headnum == HEAD_RANDOM_GENDER) {
        return null_h;
    }
    id = catalogIdByRuntime(ASSET_HEAD, headnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            return catalogEffectiveHandle(e);
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetHeadHandle: headnum=%d not in catalog", headnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: head headnum=%d not found in catalog", headnum);
    return null_h;
}

asset_data_handle_t catalogGetPropHandle(s32 propnum)
{
    const char *id;
    const asset_entry_t *e;
    asset_data_handle_t null_h;
    memset(&null_h, 0, sizeof(null_h));

    id = catalogIdByRuntime(ASSET_MODEL, propnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            return catalogEffectiveHandle(e);
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetPropHandle: propnum=%d not in catalog", propnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: prop model propnum=%d not found in catalog", propnum);
    return null_h;
}

/* Stage/arena/weapon lookup by numeric ID — DELETED (Phase 7).
 * All callers now use stage_id / weapon catalog IDs directly, or
 * inline assetCatalogGetByIndex scans at the few remaining sites
 * where numeric → catalog-ID conversion is unavoidable (match start,
 * save migration). */

/* -------------------------------------------------------------------------
 * SA-5d: Body / head property accessors (M0.1e)
 * Thin wrappers over g_HeadsAndBodies[] that make the catalog the public
 * API for body/head property reads.  The ROM array is the current internal
 * implementation detail.  Future mod overrides will intercept here.
 * O(1).  All bounds-checked, return 0 / 1.0f on out-of-range index.
 * ------------------------------------------------------------------------- */

s32 catalogGetBodyIsMale(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].ismale;
}

s32 catalogGetBodyType(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].type;
}

s32 catalogGetBodyHeight(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].height;
}

f32 catalogGetBodyAnimScale(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 1.0f; }
    return g_HeadsAndBodies[bodynum].animscale;
}

s32 catalogGetBodyCanVaryHeight(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].canvaryheight;
}

s32 catalogGetBodyIsComplete(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].unk00_01;
}

s32 catalogGetBodyHandFilenum(s32 bodynum)
{
    if (bodynum < 0 || bodynum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[bodynum].handfilenum;
}

s32 catalogGetHeadIsMale(s32 headnum)
{
    if (headnum < 0 || headnum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[headnum].ismale;
}

s32 catalogGetHeadType(s32 headnum)
{
    if (headnum < 0 || headnum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[headnum].type;
}

s32 catalogGetHeadHeight(s32 headnum)
{
    if (headnum < 0 || headnum >= 152) { return 0; }
    return (s32)g_HeadsAndBodies[headnum].height;
}

/* -------------------------------------------------------------------------
 * SA-5e: MP weapon table accessors (M0.1e)
 * Thin wrappers over g_MpWeapons[] that make the catalog the public API
 * for MP weapon property reads.  ROM array is the internal implementation.
 * O(1).  Bounds-checked, return 0 on out-of-range index.
 * ------------------------------------------------------------------------- */

s32 catalogGetMpWeaponNum(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].weaponnum;
}

s32 catalogGetMpWeaponUnlockFeature(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].unlockfeature;
}

s32 catalogGetMpWeaponPriAmmoType(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].priammotype;
}

s32 catalogGetMpWeaponPriAmmoQty(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].priammoqty;
}

s32 catalogGetMpWeaponSecAmmoType(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].secammotype;
}

s32 catalogGetMpWeaponSecAmmoQty(s32 mpweapon_idx)
{
    if (mpweapon_idx < 0 || mpweapon_idx >= NUM_MPWEAPONS) { return 0; }
    return (s32)g_MpWeapons[mpweapon_idx].secammoqty;
}

/* -------------------------------------------------------------------------
 * SA-5f: Body / head modeldef lazy-load and reset (M0.1f)
 *
 * catalogGetBodyModeldef / catalogGetHeadModeldef: lazy-load on first call,
 * cached in g_HeadsAndBodies[].modeldef.  NOT compiled for PD_SERVER builds.
 * catalogResetBodyModeldef / catalogResetHeadModeldef / catalogResetAllModeldefs:
 * clear cached pointer(s); compiled for all targets.
 * ------------------------------------------------------------------------- */

#if !defined(PD_SERVER)
struct modeldef *catalogGetBodyModeldef(s32 bodynum)
{
    s32 filenum;
    if (bodynum < 0 || bodynum >= 152) { return NULL; }
    if (!g_HeadsAndBodies[bodynum].modeldef) {
        filenum = catalogGetBodyFilenumByIndex(bodynum);
        g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNew((u16)filenum);
    }
    return g_HeadsAndBodies[bodynum].modeldef;
}

struct modeldef *catalogGetHeadModeldef(s32 headnum)
{
    s32 filenum;
    if (headnum < 0 || headnum >= 152) { return NULL; }
    if (headnum == HEAD_RANDOM_GENDER) { return NULL; }
    if (!g_HeadsAndBodies[headnum].modeldef) {
        filenum = catalogGetHeadFilenumByIndex(headnum);
        g_HeadsAndBodies[headnum].modeldef = modeldefLoadToNew((u16)filenum);
    }
    return g_HeadsAndBodies[headnum].modeldef;
}
#endif /* !PD_SERVER */

void catalogResetBodyModeldef(s32 bodynum)
{
    if (bodynum >= 0 && bodynum < 152) {
        g_HeadsAndBodies[bodynum].modeldef = NULL;
    }
}

void catalogResetHeadModeldef(s32 headnum)
{
    if (headnum >= 0 && headnum < 152) {
        g_HeadsAndBodies[headnum].modeldef = NULL;
    }
}

void catalogResetAllModeldefs(void)
{
    s32 i;
    for (i = 0; g_HeadsAndBodies[i].filenum != 0; i++) {
        g_HeadsAndBodies[i].modeldef = NULL;
    }
}
