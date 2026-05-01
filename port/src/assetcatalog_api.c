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
#include "game/challenge.h"  /* unlock-state filter for assetCatalogIterateUnlockedByType */
#include "catalog_checked.h"  /* INV-1: pure validators backing _Checked accessors */
#include "catalog_mgr_heads.h"  /* Catalog Gate 3 F2: head accessors route through manager */
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
    out->handle       = catalogEffectiveHandle(e);
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
    out->handle       = catalogEffectiveHandle(e);
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
    s32 mp_weapon_id;

    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->filenum    = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->handle     = catalogEffectiveHandle(e);
    mp_weapon_id    = e->ext.weapon.weapon_id;
    out->mp_weapon_id = mp_weapon_id;
    out->weapon_num = (e->runtime_index >= 0)
        ? e->runtime_index
        : catalogGetMpWeaponNum(mp_weapon_id);
    out->net_hash   = e->net_hash;
    out->session_id = sessionCatalogLookupWireId(e->id);
}

static void s_fillModelResult(const asset_entry_t *e, catalog_model_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->filenum    = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->handle     = catalogEffectiveHandle(e);
    out->modelnum   = e->runtime_index;
    out->net_hash   = e->net_hash;
    out->session_id = sessionCatalogLookupWireId(e->id);
}

static void s_fillPropResult(const asset_entry_t *e, catalog_prop_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->entry      = e;
    out->filenum    = (e->source_filenum >= 0) ? e->source_filenum : -1;
    out->handle     = catalogEffectiveHandle(e);
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

s32 catalogResolveModel(const char *id, catalog_model_result_t *out)
{
    const asset_entry_t *e;

    memset(out, 0, sizeof(*out));
    e = assetCatalogResolve(id);
    if (!e || e->type != ASSET_MODEL) {
        sysLogPrintf(LOG_WARNING, "[CATALOG-ERROR] catalogResolveModel: '%s' not found or wrong type",
                     id ? id : "(null)");
        return 0;
    }
    s_fillModelResult(e, out);
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
    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        e = assetCatalogGetByIndex(i);
        if (!e) continue;
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

const char *catalogStageIdByStageTableIndex(s32 stage_table_index)
{
    return catalogIdByRuntime(ASSET_MAP, stage_table_index);
}

const char *catalogStageIdBySoloStageIndex(s32 solo_stage_index)
{
    if (solo_stage_index < 0 || solo_stage_index >= NUM_SOLOSTAGES) {
        return NULL;
    }
    return catalogStageIdByStagenum((s32)g_SoloStages[solo_stage_index].stagenum);
}

const char *catalogStageIdByStagenum(s32 stagenum)
{
    s32 i;

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type != ASSET_MAP) continue;
        if (e->ext.map.stagenum == stagenum) return e->id;
    }

    return NULL;
}

const char *catalogGameModeIdByScenarioIndex(s32 scenario_index)
{
    const char *id;
    s32 i;

    if (scenario_index < 0) {
        return NULL;
    }

    id = catalogIdByRuntime(ASSET_GAMEMODE, scenario_index);
    if (id) {
        return id;
    }

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type != ASSET_GAMEMODE) continue;
        if (e->ext.gamemode.mode_id == scenario_index) return e->id;
    }

    return NULL;
}

const char *catalogWeaponIdByRuntimeWeaponNum(s32 weapon_num)
{
    const char *id;
    s32 i;

    id = catalogIdByRuntime(ASSET_WEAPON, weapon_num);
    if (id) {
        return id;
    }

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        s32 mpw;
        if (!e) continue;
        if (e->type != ASSET_WEAPON) continue;
        if (e->runtime_index == weapon_num) return e->id;
        mpw = e->ext.weapon.weapon_id;
        if (mpw >= 0 && mpw < NUM_MPWEAPONS
                && catalogGetMpWeaponNum(mpw) == weapon_num) {
            return e->id;
        }
    }

    return NULL;
}

const char *catalogWeaponIdByMpWeaponId(s32 mp_weapon_id)
{
    s32 i;

    if (mp_weapon_id < 0) {
        return NULL;
    }

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type == ASSET_WEAPON
                && e->ext.weapon.weapon_id == mp_weapon_id) {
            return e->id;
        }
    }

    return NULL;
}

const char *catalogModelIdByModelnum(s32 modelnum)
{
    return catalogIdByRuntime(ASSET_MODEL, modelnum);
}

const char *catalogBodyIdByBodynum(s32 bodynum)
{
    return catalogIdByRuntime(ASSET_BODY, bodynum);
}

const char *catalogHeadIdByHeadnum(s32 headnum)
{
    return catalogIdByRuntime(ASSET_HEAD, headnum);
}

const char *catalogIdBySourceFilenum(asset_type_e type, s32 source_filenum)
{
	s32 i;

    if (source_filenum < 0) {
        return NULL;
    }

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type != type) continue;
        if (e->source_filenum == source_filenum) return e->id;
    }

	return NULL;
}

asset_data_handle_t catalogHandleBySourceFilenum(asset_type_e type, s32 source_filenum)
{
	s32 i;
	asset_data_handle_t null_handle = ASSET_HANDLE_NULL_INIT;

	if (source_filenum <= 0) {
		return null_handle;
	}

	for (i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);

		if (!e) continue;
		if (e->type != type) continue;
		if (e->source_filenum != source_filenum) continue;

		return catalogEffectiveHandle(e);
	}

	return null_handle;
}

asset_data_handle_t catalogHandleByModelSourceFilenum(asset_type_e preferred_type, s32 source_filenum)
{
	static const asset_type_e model_source_types[] = {
		ASSET_MODEL,
		ASSET_BODY,
		ASSET_HEAD,
		ASSET_WEAPON,
		ASSET_PROP,
		ASSET_VEHICLE,
	};
	asset_data_handle_t null_handle = ASSET_HANDLE_NULL_INIT;
	s32 i;

	if (source_filenum <= 0) {
		return null_handle;
	}

	if (preferred_type != ASSET_NONE) {
		asset_data_handle_t handle = catalogHandleBySourceFilenum(preferred_type, source_filenum);

		if (!assetHandleIsNull(handle)) {
			return handle;
		}
	}

	for (i = 0; i < (s32)(sizeof(model_source_types) / sizeof(model_source_types[0])); i++) {
		if (model_source_types[i] == preferred_type) {
			continue;
		}

		asset_data_handle_t handle = catalogHandleBySourceFilenum(model_source_types[i], source_filenum);

		if (!assetHandleIsNull(handle)) {
			return handle;
		}
	}

	return null_handle;
}

static s32 s_catalogHandleEquals(asset_data_handle_t a, asset_data_handle_t b)
{
	return a.provider == b.provider
        && a.opaque[0] == b.opaque[0]
        && a.opaque[1] == b.opaque[1];
}

const char *catalogIdBySourceHandle(asset_type_e type, asset_data_handle_t handle)
{
    s32 i;

    if (assetHandleIsNull(handle)) {
        return NULL;
    }

    for (i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->type != type) continue;
        if (s_catalogHandleEquals(e->source.primary, handle)) return e->id;
        if (!assetHandleIsNull(e->source.override)
                && s_catalogHandleEquals(e->source.override, handle)) {
            return e->id;
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Unlock-filtered iteration (selector pool = catalog INTERSECT unlock-state)
 *
 * The unlock gate field lives at different offsets per type's ext payload,
 * so the helper centralises the type-to-field switch. Types without a
 * requirefeature gate (most asset types) iterate identically to
 * assetCatalogIterateByType.
 *
 * Server build: `challengeIsFeatureUnlocked` is compiled in for both targets,
 * but `assetCatalogRegisterBaseGame` is not invoked server-side, so the
 * iteration finds zero entries to emit.
 * ------------------------------------------------------------------------- */

static u32 s_entryRequireFeature(const asset_entry_t *e)
{
    switch (e->type) {
        case ASSET_ARENA:       return (u32)e->ext.arena.requirefeature;
        case ASSET_BODY:        return (u32)e->ext.body.requirefeature;
        case ASSET_HEAD:        return (u32)e->ext.head.requirefeature;
        case ASSET_WEAPON:      return (u32)e->ext.weapon.requirefeature;
        case ASSET_GAMEMODE:    return (u32)e->ext.gamemode.requirefeature;
        case ASSET_BOT_PROFILE: return (u32)e->ext.bot_profile.requirefeature;
        default:                return 0;
    }
}

typedef struct {
    asset_iter_fn user_fn;
    void         *user_data;
} unlock_filter_ctx_t;

static void s_unlockFilterCb(const asset_entry_t *e, void *userdata)
{
    unlock_filter_ctx_t *ctx = (unlock_filter_ctx_t *)userdata;
    u32 req = s_entryRequireFeature(e);
    if (req != 0 && !challengeIsFeatureUnlocked((s32)req)) {
        return;
    }
    ctx->user_fn(e, ctx->user_data);
}

void assetCatalogIterateUnlockedByType(asset_type_e type, asset_iter_fn fn,
                                        void *userdata)
{
    if (!fn) return;
    unlock_filter_ctx_t ctx;
    ctx.user_fn   = fn;
    ctx.user_data = userdata;
    assetCatalogIterateByType(type, s_unlockFilterCb, &ctx);
}

static void s_unlockCountCb(const asset_entry_t *e, void *userdata)
{
    (void)e;
    (*(s32 *)userdata)++;
}

s32 assetCatalogGetUnlockedCountByType(asset_type_e type)
{
    s32 count = 0;
    assetCatalogIterateUnlockedByType(type, s_unlockCountCb, &count);
    return count;
}

/* -------------------------------------------------------------------------
 * Music track unlock iterator (selector pool = catalog INTERSECT best-times)
 *
 * The unlock semantic for music tracks is best-time-based, distinct from
 * the feature-flag gate used by the generic assetCatalogIterateUnlockedByType.
 * Mirrors mpIsTrackUnlocked (src/game/mplayer/mplayer.c): a track is unlocked
 * iff its unlockstage is out-of-range OR the player has any best-time on
 * g_GameFile.besttimes[unlockstage].
 *
 * Server build: g_GameFile is the server stub (zero-initialised), so all
 * besttimes are 0 and the iterator emits only mod tracks (which have
 * unlockstage = -1).  Pre-cull base tracks register with positive
 * unlockstage values, so they are gated until the player finishes that
 * stage even on the server build. Matches legacy behaviour.
 * ------------------------------------------------------------------------- */

extern struct gamefile g_GameFile;

#ifndef SOLOSTAGEINDEX_SKEDARRUINS
#include "constants.h"
#endif

typedef struct {
    asset_iter_fn user_fn;
    void         *user_data;
} music_filter_ctx_t;

static int s_musicTrackUnlocked(s16 unlockstage)
{
    s32 i;
    if (unlockstage < 0) return 1;
    if (unlockstage > SOLOSTAGEINDEX_SKEDARRUINS) return 1;
    for (i = 0; i < 3; i++) {
        if (g_GameFile.besttimes[unlockstage][i] != 0) return 1;
    }
    return 0;
}

static void s_musicFilterCb(const asset_entry_t *e, void *userdata)
{
    music_filter_ctx_t *ctx = (music_filter_ctx_t *)userdata;
    if (!e || e->type != ASSET_AUDIO) return;
    if (e->ext.audio.category != AUDIO_CAT_MUSIC) return;
    if (!s_musicTrackUnlocked(e->ext.audio.unlockstage)) return;
    ctx->user_fn(e, ctx->user_data);
}

void assetCatalogIterateUnlockedMusic(asset_iter_fn fn, void *userdata)
{
    if (!fn) return;
    music_filter_ctx_t ctx;
    ctx.user_fn   = fn;
    ctx.user_data = userdata;
    assetCatalogIterateByType(ASSET_AUDIO, s_musicFilterCb, &ctx);
}

s32 assetCatalogGetUnlockedMusicCount(void)
{
    s32 count = 0;
    assetCatalogIterateUnlockedMusic(s_unlockCountCb, &count);
    return count;
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
    return catalogHeadIdByHeadnum((s32)e->ext.body.headnum);
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
 * Issue 10 (2026-04-24): per-body valid-head set, rig_class-driven.
 *
 * Compatibility is now a single rule: body.rig_class == head.rig_class
 * (exact string equality). The catalog data populates rig_class for every
 * registered body and head so the query does not need HEADBODYTYPE fallback
 * logic or category filters -- if the catalog says two entries share a rig
 * class, they pair; otherwise they don't. SP-category entries participate
 * normally, retiring the 2026-04-24 Issue 1 stopgap filter.
 *
 * Internal static buffer for the returned array of catalog ID pointers.
 * Sized so every head in the catalog fits.
 *
 * AUDIT-24-M3 (2026-04-25): the buffer is shared module-static state, so:
 *   1. NOT thread-safe -- the game is single-threaded; all catalog accessors
 *      assume that contract.
 *   2. NOT reentrant even within a single thread -- the returned pointer is
 *      a view onto s_ValidHeadBuf, which is overwritten by the next call.
 *      Callers MUST consume the array (e.g. copy IDs out, pick an index)
 *      before invoking catalogGetBodyValidHeadIds again, directly or
 *      indirectly. catalogPickRandomHeadIdForBody is one such indirect
 *      caller; nesting catalogGetBodyValidHeadIds(...) calls or calling
 *      it during iteration of a previous result will corrupt the prior
 *      view.
 *   3. The pointers inside are owned by the catalog (asset_entry_t::id),
 *      so they remain valid across the next assetCatalog* mutation only
 *      if the entry itself is not removed; treat them as borrowed.
 */
#define VALID_HEAD_BUF_CAP 256
static const char *s_ValidHeadBuf[VALID_HEAD_BUF_CAP];

typedef struct {
    const char *bodyRigClass;
    s32         count;
    s32         capacity;
    const char **out;
} valid_head_ctx_t;

static void collectValidHead(const asset_entry_t *he, void *userdata)
{
    valid_head_ctx_t *ctx = (valid_head_ctx_t *)userdata;
    if (!he || he->type != ASSET_HEAD) return;
    if (ctx->count >= ctx->capacity) return;
    if (!he->id || !he->id[0]) return;

    /* rig_class equality is the sole compatibility gate. An empty
     * rig_class on either side = incompatible (surfacing mis-authored
     * data rather than papering it over with a permissive default). */
    const char *headRig = he->ext.head.rig_class;
    if (!headRig[0] || !ctx->bodyRigClass || !ctx->bodyRigClass[0]) return;
    if (strcmp(headRig, ctx->bodyRigClass) != 0) return;

    ctx->out[ctx->count++] = he->id;
}

const char *const *catalogGetBodyValidHeadIds(const char *body_id,
                                              int *out_count)
{
    if (out_count) *out_count = 0;
    if (!body_id || !body_id[0]) return NULL;

    const asset_entry_t *be = assetCatalogResolve(body_id);
    if (!be || be->type != ASSET_BODY) return NULL;

    valid_head_ctx_t ctx;
    ctx.bodyRigClass = be->ext.body.rig_class;
    ctx.count    = 0;
    ctx.capacity = VALID_HEAD_BUF_CAP;
    ctx.out      = s_ValidHeadBuf;

    assetCatalogIterateByType(ASSET_HEAD, collectValidHead, &ctx);

    if (ctx.count == 0) {
        /* A body with zero rig-compatible heads is either (a) missing its
         * rig_class, or (b) on a rig that no head in the catalog matches.
         * Fall back to the body's declared default head so the caller gets
         * *something* playable, but log loudly -- this is a data bug, not
         * a soft situation we want to swallow. */
        sysLogPrintf(LOG_WARNING,
                "CATALOG: body '%s' (rig_class=\"%s\") has no rig-compatible "
                "heads; falling back to catalogGetBodyDefaultHead",
                body_id,
                ctx.bodyRigClass ? ctx.bodyRigClass : "");
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
 * thread a seeded RNG through from their own state.
 *
 * AUDIT-24-M2 (2026-04-25): DESYNC HAZARD outside leader-only context.
 * This function consumes from the shared g_RngState via rngRandom(); each
 * call advances the global RNG. In MP, only the leader (lobby leader for
 * pre-match picks; server for in-match picks) may call this, otherwise
 * different clients pick different heads from the same body and the
 * pre-game / mid-match RNG sequence drifts (downstream rngRandom() callers
 * see different values across peers).
 *
 * Safe call sites: lobby room screen "Set Character" path (leader only),
 * matchsetup.c::pickHeadIdForBody invoked during host-side bot config
 * generation. Unsafe: any per-client tick that runs on every peer.
 *
 * If you need a deterministic head pick on every peer (e.g. mid-match
 * resync), call catalogGetBodyValidHeadIds(...) and index it with a
 * value derived from the matchSeed or chrnum, not from rngRandom().
 *
 * Also see s_ValidHeadBuf reentrancy note above: the returned `ids`
 * pointer is invalidated by the next catalogGetBodyValidHeadIds call
 * (direct or indirect), so do not nest this function. */
const char *catalogPickRandomHeadIdForBody(const char *body_id)
{
    int count = 0;
    const char *const *ids = catalogGetBodyValidHeadIds(body_id, &count);
    if (!ids || count <= 0) return NULL;

    /* Step 2 (heads catalog migration, 2026-04-26): apply the unlock filter
     * per Mike's directive "selector pool = catalog INTERSECT unlock-state".
     * Two-pass: count unlocked first, then walk to the Nth unlocked entry.
     * Both passes resolve each ID via the catalog hash; neither re-enters
     * catalogGetBodyValidHeadIds, so the s_ValidHeadBuf reentrancy
     * contract is preserved. */
    int unlocked = 0;
    for (int i = 0; i < count; i++) {
        const asset_entry_t *e = assetCatalogResolve(ids[i]);
        if (!e || e->type != ASSET_HEAD) continue;
        u32 req = (u32)e->ext.head.requirefeature;
        if (req != 0 && !challengeIsFeatureUnlocked((s32)req)) continue;
        unlocked++;
    }

    if (unlocked == 0) {
        /* I.6 graceful fallback: a body with no rig-compatible AND unlocked
         * head still needs a defined head to render.  Fall back to the body's
         * declared default head, even if itself locked.  Better to render a
         * face the player technically hasn't unlocked than to crash or pick
         * a random off-rig head. */
        return catalogGetBodyDefaultHead(body_id);
    }

    /* rngRandom returns a u32; modulo by unlocked count picks one entry.
     * For deterministic bodies (one unlocked head) this always returns
     * the same ID. */
    u32 pick = rngRandom() % (u32)unlocked;
    int seen = 0;
    for (int i = 0; i < count; i++) {
        const asset_entry_t *e = assetCatalogResolve(ids[i]);
        if (!e || e->type != ASSET_HEAD) continue;
        u32 req = (u32)e->ext.head.requirefeature;
        if (req != 0 && !challengeIsFeatureUnlocked((s32)req)) continue;
        if (seen == (int)pick) return ids[i];
        seen++;
    }
    return ids[0];  /* unreachable -- pick was bounded by unlocked count */
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
    const char *hid = catalogHeadIdByHeadnum((s32)e->ext.body.headnum);
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

    id = catalogBodyIdByBodynum(bodynum);
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

    id = catalogHeadIdByHeadnum(headnum);
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

    id = catalogBodyIdByBodynum(bodynum);
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
    id = catalogStageIdByStageTableIndex(stageindex);
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

s32 catalogResolveModelByModelnum(s32 modelnum, catalog_model_result_t *out)
{
    const char *id;

    memset(out, 0, sizeof(*out));
    id = catalogModelIdByModelnum(modelnum);
    if (id && catalogResolveModel(id, out)) {
        return 1;
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogResolveModelByModelnum: modelnum=%d not in catalog", modelnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: model modelnum=%d not found in catalog", modelnum);
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
    return catalogGetModelFilenumByModelnum(propnum);
}

s32 catalogGetModelFilenumByModelnum(s32 modelnum)
{
    const char *id;
    const asset_entry_t *e;

    id = catalogModelIdByModelnum(modelnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            sysLogPrintf(LOG_VERBOSE, "CATALOG: %s (%d) → ROM", id, e->source_filenum);
            return e->source_filenum;
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetModelFilenumByModelnum: modelnum=%d not in catalog", modelnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: model modelnum=%d not found in catalog", modelnum);
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

    id = catalogBodyIdByBodynum(bodynum);
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
    id = catalogHeadIdByHeadnum(headnum);
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
    return catalogGetModelHandle(propnum);
}

asset_data_handle_t catalogGetModelHandle(s32 modelnum)
{
    const char *id;
    const asset_entry_t *e;
    asset_data_handle_t null_h;
    memset(&null_h, 0, sizeof(null_h));

    id = catalogModelIdByModelnum(modelnum);
    if (id) {
        e = assetCatalogResolve(id);
        if (e) {
            return catalogEffectiveHandle(e);
        }
    }
    sysLogPrintf(LOG_ERROR,
        "[CATALOG-FATAL] catalogGetModelHandle: modelnum=%d not in catalog", modelnum);
    g_CatalogFailure = 1;
    snprintf(g_CatalogFailureMsg, sizeof(g_CatalogFailureMsg),
        "CATALOG-FATAL: model modelnum=%d not found in catalog", modelnum);
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

/* Catalog Gate 3 F2: route head field reads through the manager.
 * The manager pool (s_Heads[]) mirrors g_HeadsAndBodies[] during the
 * F1-F12 parity period; F12 swaps the data source to base/heads.pdbase.
 * Tier-2 callers (body.c HEADBODYTYPE checks, chraction.c, player.c
 * vv_headheight, netmanifest.c) inherit through these accessors without
 * source changes. */

s32 catalogGetHeadIsMale(s32 headnum)
{
    const head_data_t *h = catalogManagerGetHeadByIndex(headnum);
    return h ? (s32)h->ismale : 0;
}

s32 catalogGetHeadType(s32 headnum)
{
    const head_data_t *h = catalogManagerGetHeadByIndex(headnum);
    return h ? (s32)h->type : 0;
}

s32 catalogGetHeadHeight(s32 headnum)
{
    const head_data_t *h = catalogManagerGetHeadByIndex(headnum);
    return h ? (s32)h->height : 0;
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
    const char *id;
    const asset_entry_t *e;
    if (bodynum < 0 || bodynum >= 152) { return NULL; }
    if (!g_HeadsAndBodies[bodynum].modeldef) {
        id = catalogBodyIdByBodynum(bodynum);
        e = id ? assetCatalogResolve(id) : NULL;
        g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNewFromHandle(
                catalogGetBodyHandle(bodynum),
                e ? e->source_filenum : -1);
    }
    return g_HeadsAndBodies[bodynum].modeldef;
}

struct modeldef *catalogGetHeadModeldef(s32 headnum)
{
    const char *id;
    const asset_entry_t *e;
    if (headnum < 0 || headnum >= 152) { return NULL; }
    if (headnum == HEAD_RANDOM_GENDER) { return NULL; }
    if (!g_HeadsAndBodies[headnum].modeldef) {
        id = catalogHeadIdByHeadnum(headnum);
        e = id ? assetCatalogResolve(id) : NULL;
        g_HeadsAndBodies[headnum].modeldef = modeldefLoadToNewFromHandle(
                catalogGetHeadHandle(headnum),
                e ? e->source_filenum : -1);
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

/* -------------------------------------------------------------------------
 * INV-1: _Checked accessor variants
 *
 * Loud-fail wrappers around the legacy accessors. Each returns true on
 * success + writes the value to *out, or false on miss + writes a safe
 * default to *out + emits one CATALOG.MISS WARNING line tagged with the
 * accessor name + index + reason.
 *
 * Spawn-critical paths (playerSpawn, botSpawn, body0f02ce8c,
 * bgunTickMasterLoad) MUST use these variants per Cohort A.2..A.4.
 * Non-critical readers (UI property reads, etc.) may continue to call
 * the legacy accessors.
 *
 * Rate-limit: a per-accessor static remembers the last logged index so
 * a tight loop hammering the same bad index does not spam. A different
 * bad index re-fires the warning.
 *
 * Reference: context/designs/player-init-architectural-fixes-2026-04-26.md
 * Section 2 INV-1 + Section 3 Cohort A.
 * ------------------------------------------------------------------------- */

#define CATALOG_CHECKED_LOG_MISS(funcname, idx_val, result_code) do { \
    sysLogPrintf(LOG_WARNING, "CATALOG.MISS: %s idx=%d reason=%s", \
        (funcname), (s32)(idx_val), catalogCheckedResultName(result_code)); \
} while (0)

s32 catalogGetMpWeaponNumChecked(s32 mpweapon_idx, s32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;

    if (out_value) { *out_value = 0; }
    r = catalogCheckedValidateIndex(mpweapon_idx, NUM_MPWEAPONS);
    if (r != CATALOG_CHECKED_OK) {
        if (mpweapon_idx != s_lastBadIdx) {
            s_lastBadIdx = mpweapon_idx;
            CATALOG_CHECKED_LOG_MISS("catalogGetMpWeaponNumChecked", mpweapon_idx, r);
        }
        return 0;
    }
    if (out_value) { *out_value = (s32)g_MpWeapons[mpweapon_idx].weaponnum; }
    return 1;
}

s32 catalogGetMpWeaponPriAmmoTypeChecked(s32 mpweapon_idx, s32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;

    if (out_value) { *out_value = 0; }
    r = catalogCheckedValidateIndex(mpweapon_idx, NUM_MPWEAPONS);
    if (r != CATALOG_CHECKED_OK) {
        if (mpweapon_idx != s_lastBadIdx) {
            s_lastBadIdx = mpweapon_idx;
            CATALOG_CHECKED_LOG_MISS("catalogGetMpWeaponPriAmmoTypeChecked", mpweapon_idx, r);
        }
        return 0;
    }
    if (out_value) { *out_value = (s32)g_MpWeapons[mpweapon_idx].priammotype; }
    return 1;
}

s32 catalogGetMpWeaponPriAmmoQtyChecked(s32 mpweapon_idx, s32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;

    if (out_value) { *out_value = 0; }
    r = catalogCheckedValidateIndex(mpweapon_idx, NUM_MPWEAPONS);
    if (r != CATALOG_CHECKED_OK) {
        if (mpweapon_idx != s_lastBadIdx) {
            s_lastBadIdx = mpweapon_idx;
            CATALOG_CHECKED_LOG_MISS("catalogGetMpWeaponPriAmmoQtyChecked", mpweapon_idx, r);
        }
        return 0;
    }
    if (out_value) { *out_value = (s32)g_MpWeapons[mpweapon_idx].priammoqty; }
    return 1;
}

s32 catalogGetBodyScaleChecked(s32 bodynum, f32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;
    s32 sentinel;

    if (out_value) { *out_value = 1.0f; }
    sentinel = (bodynum >= 0 && bodynum < 152) ? (s32)g_HeadsAndBodies[bodynum].filenum : 0;
    r = catalogCheckedValidateSlot(bodynum, 152, sentinel);
    if (r != CATALOG_CHECKED_OK) {
        if (bodynum != s_lastBadIdx) {
            s_lastBadIdx = bodynum;
            CATALOG_CHECKED_LOG_MISS("catalogGetBodyScaleChecked", bodynum, r);
        }
        return 0;
    }
    if (out_value) { *out_value = catalogGetBodyScaleByIndex(bodynum); }
    return 1;
}

s32 catalogGetBodyAnimScaleChecked(s32 bodynum, f32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;
    s32 sentinel;

    if (out_value) { *out_value = 1.0f; }
    sentinel = (bodynum >= 0 && bodynum < 152) ? (s32)g_HeadsAndBodies[bodynum].filenum : 0;
    r = catalogCheckedValidateSlot(bodynum, 152, sentinel);
    if (r != CATALOG_CHECKED_OK) {
        if (bodynum != s_lastBadIdx) {
            s_lastBadIdx = bodynum;
            CATALOG_CHECKED_LOG_MISS("catalogGetBodyAnimScaleChecked", bodynum, r);
        }
        return 0;
    }
    if (out_value) { *out_value = g_HeadsAndBodies[bodynum].animscale; }
    return 1;
}

s32 catalogGetBodyHandFilenumChecked(s32 bodynum, s32 *out_value)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;
    s32 sentinel;

    if (out_value) { *out_value = 0; }
    sentinel = (bodynum >= 0 && bodynum < 152) ? (s32)g_HeadsAndBodies[bodynum].filenum : 0;
    r = catalogCheckedValidateSlot(bodynum, 152, sentinel);
    if (r != CATALOG_CHECKED_OK) {
        if (bodynum != s_lastBadIdx) {
            s_lastBadIdx = bodynum;
            CATALOG_CHECKED_LOG_MISS("catalogGetBodyHandFilenumChecked", bodynum, r);
        }
        return 0;
    }
    if (out_value) { *out_value = (s32)g_HeadsAndBodies[bodynum].handfilenum; }
    return 1;
}

#if !defined(PD_SERVER)
s32 catalogGetBodyModeldefChecked(s32 bodynum, struct modeldef **out_md)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;
    s32 sentinel;
    struct modeldef *md;

    if (out_md) { *out_md = NULL; }
    sentinel = (bodynum >= 0 && bodynum < 152) ? (s32)g_HeadsAndBodies[bodynum].filenum : 0;
    r = catalogCheckedValidateSlot(bodynum, 152, sentinel);
    if (r != CATALOG_CHECKED_OK) {
        if (bodynum != s_lastBadIdx) {
            s_lastBadIdx = bodynum;
            CATALOG_CHECKED_LOG_MISS("catalogGetBodyModeldefChecked", bodynum, r);
        }
        return 0;
    }
    md = catalogGetBodyModeldef(bodynum);
    if (!md) {
        /* Slot is registered but the lazy-load failed (ROM file missing,
         * mod asset torn, etc.). Distinct from OOB / unpopulated. */
        if (bodynum != s_lastBadIdx) {
            s_lastBadIdx = bodynum;
            sysLogPrintf(LOG_WARNING,
                "CATALOG.MISS: catalogGetBodyModeldefChecked bodynum=%d reason=loadfail",
                bodynum);
        }
        return 0;
    }
    if (out_md) { *out_md = md; }
    return 1;
}

s32 catalogGetHeadModeldefChecked(s32 headnum, struct modeldef **out_md)
{
    catalog_checked_result_e r;
    static s32 s_lastBadIdx = -1;
    s32 sentinel;
    struct modeldef *md;

    if (out_md) { *out_md = NULL; }
    /* HEAD_RANDOM_GENDER is a sentinel-out-of-band, not a miss. */
    if (headnum == HEAD_RANDOM_GENDER) { return 0; }
    sentinel = (headnum >= 0 && headnum < 152) ? (s32)g_HeadsAndBodies[headnum].filenum : 0;
    r = catalogCheckedValidateSlot(headnum, 152, sentinel);
    if (r != CATALOG_CHECKED_OK) {
        if (headnum != s_lastBadIdx) {
            s_lastBadIdx = headnum;
            CATALOG_CHECKED_LOG_MISS("catalogGetHeadModeldefChecked", headnum, r);
        }
        return 0;
    }
    md = catalogGetHeadModeldef(headnum);
    if (!md) {
        if (headnum != s_lastBadIdx) {
            s_lastBadIdx = headnum;
            sysLogPrintf(LOG_WARNING,
                "CATALOG.MISS: catalogGetHeadModeldefChecked headnum=%d reason=loadfail",
                headnum);
        }
        return 0;
    }
    if (out_md) { *out_md = md; }
    return 1;
}
#endif /* !PD_SERVER */

s32 catalogGetStageResultByIndexChecked(s32 stageindex, catalog_stage_result_t *out)
{
    static s32 s_lastBadIdx = -1;
    s32 ok;

    if (out) { memset(out, 0, sizeof(*out)); }
    /* Underlying call already logs ERROR + sets g_CatalogFailure on miss.
     * Wrap to bool + add a CATALOG.MISS line at WARNING level so the
     * spawn-path logs are tagged consistently. */
    ok = catalogGetStageResultByIndex(stageindex, out);
    if (!ok) {
        if (stageindex != s_lastBadIdx) {
            s_lastBadIdx = stageindex;
            sysLogPrintf(LOG_WARNING,
                "CATALOG.MISS: catalogGetStageResultByIndexChecked stageindex=%d reason=resolve",
                stageindex);
        }
        return 0;
    }
    return 1;
}
