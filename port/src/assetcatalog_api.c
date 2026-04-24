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
#include "lib/rng.h"  /* P3: rngRandom for catalogPickRandomHeadIdForBody (client-only) */
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

/* -------------------------------------------------------------------------
 * P3 (2026-04-24): per-body valid-head set.
 *
 * Enumerate every head whose HEADBODYTYPE_* is compatible with the body's
 * type.  Compatibility rules mirror modelcatalog.c::catalogIsHeadBodyCompatible
 * (same type, or DEFAULT+DEFAULT, or FEMALE/FEMALEGUARD cross-pair).  FEMALE
 * and DEFAULT are distinct types in the catalog, so type match alone already
 * avoids cross-gender pairs.
 *
 * Internal static buffer for the returned array of catalog ID pointers.
 * Sized generously so every head in the catalog fits -- bumped from 128 to
 * 256 to absorb future mod-authored heads without a rebuild.  Non-thread-safe
 * by design (the game is single-threaded; all catalog accessors assume the
 * same contract). */
#define VALID_HEAD_BUF_CAP 256
static const char *s_ValidHeadBuf[VALID_HEAD_BUF_CAP];

typedef struct {
    s32         bodyType;
    s32         count;
    s32         capacity;
    const char **out;
} valid_head_ctx_t;

static void collectValidHead(const asset_entry_t *he, void *userdata)
{
    valid_head_ctx_t *ctx = (valid_head_ctx_t *)userdata;
    if (!he || he->type != ASSET_HEAD) return;
    if (ctx->count >= ctx->capacity) return;

    /* Issue 1 fix (2026-04-24): exclude SP-only heads from every
     * body's valid set.  ASSET_HEAD entries registered as
     * "base:sp_head_<engine_idx>" by assetCatalogRegisterBaseGame are
     * tagged with `category == "sp"`; real MP-selectable heads use
     * `category == "base"`.  Letting SP heads into a valid-set meant
     * Mike's playtest saw bots with bodies like `dd_guard` (human)
     * getting `sp_head_52` -> mphead=0 (Joanna fallback), producing
     * the "stewardess has Joanna's head" symptom.
     *
     * Using the `category` field rather than `mp_index >= 0` is the
     * right gate because of a data quirk: s_BaseHeads[] only maps
     * 75 of the 76 MP slots to names; the MP registration pass
     * registers the 76th as `base:head_<mpidx>` but the SP loop
     * ALSO registers the same engine head as `base:sp_head_<idx>`,
     * and the SP registration wins the `s_RuntimeCache[ASSET_HEAD]`
     * slot.  Pass 3 of the runtime-cache builder (see
     * `catalogBuildRuntimeCaches`) then copies `mp_index = 75`
     * onto the SP-registered entry -- so `sp_head_21` ends up with
     * a non-negative mp_index despite being an SP entry.  The
     * category field avoids that ambiguity.
     *
     * Consequence: a body's valid set is now exactly the
     * HEADBODYTYPE-compatible heads registered as "base" category.
     * For Maian bodies that means head_elvis + head_maian_s (two);
     * for human male bodies the full g_MpMaleHeads pool (~43); for
     * human female bodies the g_MpFemaleHeads pool (7).  If Mike
     * wants more Maian variety, the fix is to promote an SP Maian
     * head into the MP list (add an entry to `s_BaseHeads` with a
     * spare `g_MpHeads[]` mpidx); that is a data-authoring change,
     * not a code change.
     *
     * Mod heads are unaffected -- mod scanners register with their
     * own category string ("mod" or similar), and their valid-set
     * inclusion rides on the same HEADBODYTYPE rules.  Only the
     * literal "sp" category is rejected. */
    if (he->category[0] == 's' && he->category[1] == 'p' && he->category[2] == '\0') {
        return;
    }

    s32 headnum = (s32)he->ext.head.headnum;
    s32 headType = catalogGetHeadType(headnum);

    s32 compatible = 0;
    if (headType == ctx->bodyType) {
        compatible = 1;
    } else if (headType == HEADBODYTYPE_DEFAULT && ctx->bodyType == HEADBODYTYPE_DEFAULT) {
        compatible = 1;
    } else if ((headType == HEADBODYTYPE_FEMALE || headType == HEADBODYTYPE_FEMALEGUARD)
            && (ctx->bodyType == HEADBODYTYPE_FEMALE || ctx->bodyType == HEADBODYTYPE_FEMALEGUARD)) {
        compatible = 1;
    }

    if (compatible && he->id && he->id[0]) {
        ctx->out[ctx->count++] = he->id;
    }
}

const char *const *catalogGetBodyValidHeadIds(const char *body_id,
                                              int *out_count)
{
    if (out_count) *out_count = 0;
    if (!body_id || !body_id[0]) return NULL;

    const asset_entry_t *be = assetCatalogResolve(body_id);
    if (!be || be->type != ASSET_BODY) return NULL;

    valid_head_ctx_t ctx;
    ctx.bodyType = catalogGetBodyType((s32)be->ext.body.bodynum);
    ctx.count    = 0;
    ctx.capacity = VALID_HEAD_BUF_CAP;
    ctx.out      = s_ValidHeadBuf;

    assetCatalogIterateByType(ASSET_HEAD, collectValidHead, &ctx);

    if (ctx.count == 0) {
        /* Defensive fallback: deterministic body without a matching type
         * (should not happen in well-authored catalogs; surface loudly if
         * it does, then fall back to the body's declared default head so
         * the caller gets *something*). */
        sysLogPrintf(LOG_WARNING,
                "CATALOG: body '%s' (type=%d) has no type-compatible heads; "
                "falling back to catalogGetBodyDefaultHead",
                body_id, (s32)ctx.bodyType);
        const char *fallback = catalogGetBodyDefaultHead(body_id);
        if (fallback) {
            s_ValidHeadBuf[0] = fallback;
            ctx.count = 1;
        }
    }

    if (out_count) *out_count = ctx.count;
    return ctx.count > 0 ? s_ValidHeadBuf : NULL;
}

#if !defined(PD_SERVER)
/* Client-only: the server has no rngRandom symbol (rng_c.c is not linked
 * into pd-server).  Server callers that need a head-for-body picker can
 * call catalogGetBodyValidHeadIds(...) directly and pick element 0, or
 * thread a seeded RNG through from their own state. */
const char *catalogPickRandomHeadIdForBody(const char *body_id)
{
    int count = 0;
    const char *const *ids = catalogGetBodyValidHeadIds(body_id, &count);
    if (!ids || count <= 0) return NULL;
    /* rngRandom returns a u32; modulo by count picks one entry.  For
     * deterministic bodies (count == 1) this always returns the same ID. */
    u32 pick = rngRandom() % (u32)count;
    return ids[(s32)pick];
}
#endif

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
