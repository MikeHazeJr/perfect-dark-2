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
    s32 weapons_registered;   /* successfully registered records */
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
 * Phase 2 (F10): with no .pdbase entries registered yet, this is a
 * no-op that returns 0 records loaded. F11+ implements the actual
 * record decode. */
s32 loaderPdbaseBuildWeaponManager(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_PDBASE_H */
