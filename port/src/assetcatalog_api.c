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
#include "net/sessioncatalog.h"
#include "net/netbuf.h"

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
    } else {
        /* Server build: g_Stages is NULL; file IDs not available. */
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
    if (!e || e->type != ASSET_MAP) {
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

/* -------------------------------------------------------------------------
 * SA-4: Reverse-index lookup (save migration only)
 * O(n) scan of the entry pool.  Used only during legacy save conversion.
 * ------------------------------------------------------------------------- */

const char *catalogResolveByRuntimeIndex(asset_type_e type, s32 runtime_index)
{
    s32 i;
    const asset_entry_t *e;

    for (i = 0; ; i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) break;
        if (e->type == type && e->runtime_index == runtime_index) {
            return e->id;
        }
    }

    sysLogPrintf(LOG_WARNING,
        "[CATALOG-ASSERT] catalogResolveByRuntimeIndex: type=%d index=%d not found",
        (int)type, runtime_index);
    return NULL;
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

    id = catalogResolveByRuntimeIndex(ASSET_BODY, bodynum);
    if (id && catalogResolveBody(id, &result)) {
        return result.filenum;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetBodyFilenumByIndex: bodynum=%d not in catalog "
        "(searched ASSET_BODY by runtime_index)", bodynum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: body bodynum=%d not found in catalog", bodynum);
    return 0;
}

s32 catalogGetHeadFilenumByIndex(s32 headnum)
{
    const char *id;
    catalog_head_result_t result;

    id = catalogResolveByRuntimeIndex(ASSET_HEAD, headnum);
    if (id && catalogResolveHead(id, &result)) {
        return result.filenum;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetHeadFilenumByIndex: headnum=%d not in catalog "
        "(searched ASSET_HEAD by runtime_index)", headnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: head headnum=%d not found in catalog", headnum);
    return 0;
}

f32 catalogGetBodyScaleByIndex(s32 bodynum)
{
    const char *id;
    catalog_body_result_t result;

    id = catalogResolveByRuntimeIndex(ASSET_BODY, bodynum);
    if (id && catalogResolveBody(id, &result)) {
        return result.model_scale;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetBodyScaleByIndex: bodynum=%d not in catalog "
        "(searched ASSET_BODY by runtime_index), falling back to legacy scale",
        bodynum);
    return g_HeadsAndBodies[bodynum].scale;
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
    id = catalogResolveByRuntimeIndex(ASSET_MAP, stageindex);
    if (id && catalogResolveStage(id, out)) {
        return 1;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetStageResultByIndex: stageindex=%d not in catalog "
        "(searched ASSET_MAP by runtime_index)", stageindex);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: stage stageindex=%d not found in catalog", stageindex);
    return 0;
}
