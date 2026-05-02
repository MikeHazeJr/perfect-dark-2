#ifndef PD_LOADER_PDBASE_H
#define PD_LOADER_PDBASE_H

#include <PR/ultratypes.h>

/*
 * port/include/loader_pdbase.h -- S484 F10: .pdbase loader skeleton.
 *
 * Scaffold for the eager-build / lazy-read loader described in
 * context/designs/catalog-full-pipeline-weapons-2026-04-27.md
 * Section E.
 *
 * Phase 2 (F1-F10): the loader is a skeleton that scans an empty
 * `base/` directory at startup and yields zero records. The catalog
 * manager continues to source weapon data from g_Weapons[] (live
 * router parity). F11+ (next session) will:
 *   1. Move all 86 weapon definitions from invitems.c to
 *      `base/weapons.pdbase` JSON.
 *   2. Have the loader populate the manager from .pdbase records.
 *   3. Retire g_Weapons[], g_AibotWeaponPreferences[],
 *      invaimsettings_default, invnoisesettings_silent, and the
 *      invitem_* / invfunc_* / invammo_* static records.
 *
 * Logging channels (per directive):
 *   LOADER.PDBASE.WEAPON.SCAN_FAIL:    archive open / parse error.
 *   LOADER.PDBASE.WEAPON.RESOLVE_FAIL: model / anim / ammo ref unresolvable.
 *   LOADER.PDBASE.WEAPON.FIELD_UNKNOWN: unknown field in record (debug).
 *   LOADER.PDBASE.WEAPON.OK:           end-of-load summary.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Result struct from a loader pass.  Counts per outcome bucket so the
 * caller can print a single OK summary. */
typedef struct {
    s32 archives_scanned;     /* number of .pdbase archives examined */
    s32 weapons_registered;   /* successfully registered weapon records */
    s32 heads_registered;     /* successfully registered head records (Catalog Gate 3 F9) */
    s32 bodies_registered;    /* successfully registered body records (Catalog Gate 3 Bodies F9) */
    s32 scan_failures;        /* parse / open errors per record */
    s32 resolve_failures;     /* model / anim / ammo ref unresolvable */
    s32 field_unknown;        /* PER-ELEMENT field-unknown notes */
} loader_pdbase_result_t;

/* Scan `dir` for `*.pdbase` archives and register lightweight catalog
 * rows for each weapon record found. Idempotent: safe to call multiple
 * times. Returns counts via `out` (zeroed if NULL).
 *
 * Phase 2 (F10): the directory scan is implemented but archives are
 * not yet decoded. An empty or absent dir returns zeros and emits
 * a single OK log line. */
void loaderPdbaseScan(const char *dir, loader_pdbase_result_t *out);

/* Iterate ASSET_WEAPON catalog rows that carry a non-empty pdbase_path
 * and load each weapon's full data into the catalog manager.
 *
 * F12 implementation: switches the loader to "active" mode so that
 * subsequent loaderPdbaseGetWeapon(idx) calls return the pool-backed
 * struct weapon instead of NULL. The catalog manager
 * (catalog_mgr_weapons.c) checks loaderPdbaseIsActive() to decide
 * whether to route through the loader or fall back to g_Weapons[]. */
s32 loaderPdbaseBuildWeaponManager(void);

/* Manager-side accessors. The loader owns the typed pools; the
 * manager queries through these helpers. After F13 retires
 * g_Weapons[], loaderPdbaseGetWeapon() is the only weapon-data
 * source. */
struct weapon;
struct invaimsettings;
struct noisesettings;
struct guncmd;
struct aibotweaponpreference;

s32 loaderPdbaseIsActive(void);
const struct weapon                *loaderPdbaseGetWeapon(s32 idx);
const struct invaimsettings        *loaderPdbaseGetDefaultAim(void);
const struct noisesettings         *loaderPdbaseGetDefaultNoise(void);
const struct aibotweaponpreference *loaderPdbaseGetBotPref(s32 idx);
s32 loaderPdbaseGetWeaponsRegistered(void);

/* S484-followup-3 (2026-05-01): pool-range + canary accessors for the
 * fire-time recoil crash investigation. See loader_pdbase.c for full
 * rationale; bondgun.c's recoil-block instrumentation uses these to
 * detect a wild shootfunc / recoilsettings pointer or a buffer-overrun
 * canary trip before the dereference faults. */
const void *loaderPdbaseGetRecoilSettingsBase(void);
const void *loaderPdbaseGetRecoilSettingsEnd(void);
const void *loaderPdbaseGetWeaponFuncsBase(void);
const void *loaderPdbaseGetWeaponFuncsEnd(void);
u32 loaderPdbaseCheckCanaries(void);

/* F12 round-trip helper: encode a single struct guncmd back to a
 * JSON-ish string ("[mnem, unk01, unk02, unk04]"). Used by tests. */
#include <stddef.h>
s32 loaderPdbaseEncodeOpcode(const struct guncmd *cmd, char *out_buf,
                              size_t out_n);

/* ========================================================================
 * Catalog Gate 3 F9: heads-side loader scaffold.
 *
 * Parallels the weapons accessors above. Heads live in their own archive
 * (base/heads.pdbase) per audit decision I.6, and have their own typed
 * pool s_Heads_loader[CATALOG_MGR_HEAD_COUNT] of head_data_t records.
 *
 * F9 ships the scaffold (zero records); F11 ships the Python extractor +
 * archive; F12 implements the parser + parity bridge; F13 retires the
 * parity bridge.
 *
 * Logging channels:
 *   LOADER.PDBASE.HEAD.SCAN_FAIL
 *   LOADER.PDBASE.HEAD.RESOLVE_FAIL
 *   LOADER.PDBASE.HEAD.FIELD_UNKNOWN
 *   LOADER.PDBASE.HEAD.OK
 * ======================================================================== */

/* Forward-declare to avoid pulling in catalog_mgr_heads.h from this header. */
typedef struct head_data head_data_t;

/* Manager-side accessors. The catalog manager
 * (catalog_mgr_heads.c::s_get) checks loaderPdbaseHeadsActive() to
 * decide whether to route through the loader pool or fall back to
 * g_HeadsAndBodies[]. */
s32 loaderPdbaseHeadsActive(void);
const head_data_t *loaderPdbaseGetHead(s32 idx);
s32 loaderPdbaseGetHeadsRegistered(void);

/* Iterate ASSET_HEAD catalog rows that carry a non-empty pdbase_path
 * and load each head's full data into the loader pool.  After this
 * call, loaderPdbaseHeadsActive() returns 1 and the manager routes
 * through loaderPdbaseGetHead(). */
s32 loaderPdbaseBuildHeadManager(void);

/* ========================================================================
 * Catalog Gate 3 Bodies F9: bodies-side loader scaffold.
 *
 * Parallels the heads accessors above. Bodies live in their own archive
 * (base/bodies.pdbase) per audit decision I.6, and have their own typed
 * pool s_BodiesPool[CATALOG_MGR_BODY_COUNT] of body_data_t records.
 *
 * F9 ships the scaffold (zero records); F11 ships the Python extractor +
 * archive; F12 implements the parser + parity bridge; F13 retires the
 * parity bridge.
 *
 * Logging channels:
 *   LOADER.PDBASE.BODY.SCAN_FAIL
 *   LOADER.PDBASE.BODY.RESOLVE_FAIL
 *   LOADER.PDBASE.BODY.FIELD_UNKNOWN
 *   LOADER.PDBASE.BODY.OK
 * ======================================================================== */

/* Forward-declare to avoid pulling in catalog_mgr_bodies.h from this header. */
typedef struct body_data body_data_t;

/* Manager-side accessors. The catalog manager
 * (catalog_mgr_bodies.c::s_get) checks loaderPdbaseBodiesActive() to
 * decide whether to route through the loader pool or fall back to
 * g_HeadsAndBodies[]. */
s32 loaderPdbaseBodiesActive(void);
const body_data_t *loaderPdbaseGetBody(s32 idx);
s32 loaderPdbaseGetBodiesRegistered(void);

/* Iterate ASSET_BODY catalog rows that carry a non-empty pdbase_path
 * and load each body's full data into the loader pool.  After this
 * call, loaderPdbaseBodiesActive() returns 1 and the manager routes
 * through loaderPdbaseGetBody(). */
s32 loaderPdbaseBuildBodyManager(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_PDBASE_H */
