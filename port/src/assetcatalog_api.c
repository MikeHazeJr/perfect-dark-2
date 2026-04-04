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

    static const char *s_typeNames[] = {
        "NONE","MAP","CHARACTER","SKIN","BOT_VARIANT","WEAPON","TEXTURES","SFX",
        "MUSIC","PROP","VEHICLE","MISSION","UI","TOOL","ARENA","BODY","HEAD",
        "ANIMATION","TEXTURE","GAMEMODE","AUDIO","HUD","EFFECT","MODEL","LANG"
    };
    const char *tname = ((int)type >= 0 && (int)type < (s32)(sizeof(s_typeNames)/sizeof(s_typeNames[0])))
                        ? s_typeNames[(int)type] : "UNKNOWN";
    sysLogPrintf(LOG_WARNING,
        "[CATALOG-ASSERT] catalogResolveByRuntimeIndex: type=%s(%d) index=%d not found",
        tname, (int)type, runtime_index);
    return NULL;
}

/* -------------------------------------------------------------------------
 * B.2: MP index domain helpers
 * Convert mpbodynum (g_MpBodies[] position, 0..62) or mpheadnum (g_MpHeads[]
 * position, 0..75) to the g_HeadsAndBodies[] index stored as runtime_index,
 * then delegate to catalogResolveByRuntimeIndex.
 *
 * Always use these instead of catalogResolveByRuntimeIndex(ASSET_BODY/HEAD, mpN)
 * — mpbodynum and runtime_index (bodynum) are different index spaces.
 * ------------------------------------------------------------------------- */

extern struct mpbody g_MpBodies[];
extern struct mphead g_MpHeads[];

const char *catalogResolveBodyByMpIndex(s32 mpbodynum)
{
    if (mpbodynum < 0 || mpbodynum >= 63) {
        sysLogPrintf(LOG_WARNING,
            "[CATALOG] catalogResolveBodyByMpIndex: mpbodynum=%d out of range [0,63)",
            mpbodynum);
        return NULL;
    }
    return catalogResolveByRuntimeIndex(ASSET_BODY, (s32)g_MpBodies[mpbodynum].bodynum);
}

const char *catalogResolveHeadByMpIndex(s32 mpheadnum)
{
    if (mpheadnum < 0 || mpheadnum >= 76) {
        sysLogPrintf(LOG_WARNING,
            "[CATALOG] catalogResolveHeadByMpIndex: mpheadnum=%d out of range [0,76)",
            mpheadnum);
        return NULL;
    }
    return catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)g_MpHeads[mpheadnum].headnum);
}

/* FIX-12: Reverse lookup — g_HeadsAndBodies[] index (runtime_index) → g_MpBodies[]
 * position.  Used at save-load sites where a catalog entry is resolved and its
 * runtime_index needs to be converted back to mpbodynum for game config structs.
 * Returns -1 if bodynum is not in g_MpBodies[]. */
s32 catalogBodynumToMpBodyIdx(s32 bodynum)
{
    s32 i;
    for (i = 0; i < 63; i++) {
        if ((s32)g_MpBodies[i].bodynum == bodynum) return i;
    }
    return -1;
}

/* FIX-12: Reverse lookup — g_HeadsAndBodies[] index → g_MpHeads[] position.
 * Returns -1 if headnum is not in g_MpHeads[]. */
s32 catalogHeadnumToMpHeadIdx(s32 headnum)
{
    s32 i;
    for (i = 0; i < 76; i++) {
        if ((s32)g_MpHeads[i].headnum == headnum) return i;
    }
    return -1;
}

/* Body → default head catalog ID.  Reads ext.body.headnum from the body catalog
 * entry and resolves it to a HEAD catalog ID string.  Used by UI character pickers
 * (matchsetup bot editor, agentcreate carousel) so they never guess the head from
 * the body index.  Returns NULL if body_id is unknown, wrong type, or headnum < 0. */
const char *catalogGetBodyDefaultHead(const char *body_id)
{
    const asset_entry_t *e;
    if (!body_id || !body_id[0]) return NULL;
    e = assetCatalogResolve(body_id);
    if (!e || e->type != ASSET_BODY) return NULL;
    if (e->ext.body.headnum < 0) return NULL;
    return catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)e->ext.body.headnum);
}

/* Body → default head mpheadnum.  Convenience wrapper for UI carousels that track
 * position in g_MpHeads[] rather than catalog ID strings.
 * Returns -1 if any step fails (unknown body, no head registered, headnum not in
 * g_MpHeads[] — this includes the sentinel value 1000 used for random-gender heads
 * since those have no single deterministic catalog entry). */
s32 catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum)
{
    const char *bid;
    const asset_entry_t *e;
    if (mpbodynum < 0) return -1;
    bid = catalogResolveBodyByMpIndex(mpbodynum);
    if (!bid) return -1;
    e = assetCatalogResolve(bid);
    if (!e || e->type != ASSET_BODY) return -1;
    if (e->ext.body.headnum < 0) return -1;
    return catalogHeadnumToMpHeadIdx((s32)e->ext.body.headnum);
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
 * Phase C: Pre-session-catalog wire helpers
 * Used at CLC_LOBBY_START boundaries where the session catalog has not been
 * built yet.  We encode by net_hash (CRC32 of the catalog ID) which is stable
 * across both client and server because both share the same base catalog data
 * for arenas (g_MpArenas[]) and weapons (s_BaseWeapons[] static table).
 *
 * NOTE: body/head assets are NOT encodable this way — the server
 * zero-initialises g_HeadsAndBodies[] (server_stubs.c:326) so the server
 * catalog has no ASSET_BODY/HEAD entries (server catalog gap, fixed in Phase D).
 * Bot body/head in CLC_LOBBY_START is still sent as raw mpbodynum u8 with the
 * bodynum→mpbodynum conversion applied at the write site (FIX-5).
 * ------------------------------------------------------------------------- */

/* DEPRECATED (v27-04-02): net_hash wire format replaced by catalog ID strings.
 * No live callers remain in wire paths. Retained only to avoid link errors
 * if any tool or test binary still references these symbols. Remove on next cleanup. */
void catalogWritePreSessionRef(struct netbuf *buf, const char *id)
{
    const asset_entry_t *e = id ? assetCatalogResolve(id) : NULL;
    netbufWriteU32(buf, e ? e->net_hash : 0u);
}

/* DEPRECATED (v27-04-02): see catalogWritePreSessionRef. */
const asset_entry_t *catalogReadPreSessionRef(struct netbuf *buf)
{
    const u32 hash = netbufReadU32(buf);
    if (hash == 0u) return NULL;
    return catalogResolveByNetHash(hash);
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
        sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, result.filenum);
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
        sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, result.filenum);
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
        "(searched ASSET_BODY by runtime_index)", bodynum);
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

    id = catalogResolveByRuntimeIndex(ASSET_MODEL, propnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, e->source_filenum);
            return e->source_filenum;
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetPropFilenumByIndex: propnum=%d not in catalog "
        "(searched ASSET_MODEL by runtime_index)", propnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: prop model propnum=%d not found in catalog", propnum);
    return 0;
}

/* -------------------------------------------------------------------------
 * Phase 0: Canonical-ID lookups by game-internal numeric identifiers.
 * These replace the old "stage_0x%02x" / "weapon_%d" alias string pattern.
 * O(n) scans -- acceptable for manifest build paths (infrequent, not per-frame).
 * ------------------------------------------------------------------------- */

/**
 * Return the canonical catalog ID for the ASSET_MAP entry whose
 * ext.stage.stagenum equals stagenum, or NULL if not found.
 * Replaces assetCatalogResolve("stage_0x%02x") in manifest code.
 */
const char *catalogResolveStageByStagenum(s32 stagenum)
{
    s32 i;
    const asset_entry_t *e;
    for (i = 0; ; i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) break;
        if (e->type == ASSET_MAP && e->ext.map.stagenum == stagenum) {
            return e->id;
        }
    }
    return NULL;
}

/**
 * FIX-1/2 (Phase C): Return the canonical catalog ID for the ASSET_ARENA entry
 * whose ext.arena.stagenum equals stagenum, or NULL if not found.
 * Used in CLC_LOBBY_START write to map MP stagenum to a catalog entry
 * so we can send net_hash instead of a raw u8.
 * Unlike catalogResolveStageByStagenum() which targets ASSET_MAP (solo stages),
 * this searches ASSET_ARENA entries (MP arenas from g_MpArenas[]).
 * The server has g_MpArenas[] defined so this lookup succeeds on both sides.
 */
const char *catalogResolveArenaByStagenum(s32 stagenum)
{
    s32 i;
    const asset_entry_t *e;
    for (i = 0; ; i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) break;
        if (e->type == ASSET_ARENA && e->ext.arena.stagenum == stagenum) {
            return e->id;
        }
    }
    return NULL;
}

/**
 * Return the canonical catalog ID for the ASSET_WEAPON entry whose
 * ext.weapon.weapon_id equals weapon_id (an MPWEAPON_* constant), or NULL.
 * Replaces assetCatalogResolve("weapon_%d") in manifest code.
 */
const char *catalogResolveWeaponByGameId(s32 weapon_id)
{
    s32 i;
    const asset_entry_t *e;
    for (i = 0; ; i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) break;
        if (e->type == ASSET_WEAPON && e->ext.weapon.weapon_id == weapon_id) {
            return e->id;
        }
    }
    return NULL;
}
