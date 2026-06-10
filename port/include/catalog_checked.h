#ifndef PD_CATALOG_CHECKED_H
#define PD_CATALOG_CHECKED_H

#include <PR/ultratypes.h>
#include <stdbool.h>

/*
 * catalog_checked.h -- INV-1 (player-init-architectural-fixes 2026-04-26):
 * pure validators backing the `_Checked` catalog accessor variants.
 *
 * Background. The legacy accessors `catalogGetMpWeaponNum`,
 * `catalogGetBodyScaleByIndex`, etc. silently return 0 / 1.0f / NULL on
 * out-of-bounds index or in-bounds-but-unpopulated slot. The B-219 v2
 * narrative ("user picked base:remotemine but saw Falcon (Silenced)")
 * is direct evidence that these silent zeros leak into spawn-with-weapon
 * resolution and the player ends up unarmed without diagnosis.
 *
 * INV-1 makes catalog access loud on miss. The `_Checked` wrappers in
 * `port/src/assetcatalog_api.c` delegate to the pure validators here so
 * the validation logic itself can be exercised by `pd-tests` without
 * dragging in `g_MpWeapons[]` / `g_HeadsAndBodies[]` / sysLogPrintf /
 * the catalog runtime.
 *
 * Two failure modes are distinguished:
 *   CATALOG_CHECKED_OOB         -- index outside the array bounds entirely.
 *   CATALOG_CHECKED_UNPOPULATED -- index in array bounds but the slot's
 *                                  sentinel field (filenum) is 0,
 *                                  meaning the catalog never registered
 *                                  this slot. Reading other fields would
 *                                  return BSS / heap-garbage values.
 *
 * Callers receive a `catalog_checked_result_e` from the validator; the
 * spawn-critical wrappers translate this into a boolean ok-or-miss plus
 * a `CATALOG.MISS:` LOG_WARNING tagged with the accessor name + index
 * + reason.
 *
 * The validators are intentionally trivial. Their value is not in
 * complexity but in being the SINGLE point where the bounds + sentinel
 * decision lives, so the test suite can pin it.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CATALOG_CHECKED_OK           = 0,
	CATALOG_CHECKED_OOB          = 1,
	CATALOG_CHECKED_UNPOPULATED  = 2,
} catalog_checked_result_e;

/*
 * Validate an index against an array bound. No sentinel check; suitable
 * for fixed-size enum-indexed tables like g_MpWeapons (every slot in
 * [0, NUM_MPWEAPONS) is populated by catalog load).
 *
 * Returns CATALOG_CHECKED_OK if 0 <= idx < count, else CATALOG_CHECKED_OOB.
 */
catalog_checked_result_e catalogCheckedValidateIndex(s32 idx, s32 count);

/*
 * Validate an index against an array bound AND a sentinel field. Suitable
 * for variable-population tables like g_HeadsAndBodies, where a slot
 * with sentinel_value == 0 (filenum == 0) is the catalog's "this slot
 * was never registered" marker.
 *
 * Returns CATALOG_CHECKED_OK on (in-bounds AND populated),
 *         CATALOG_CHECKED_OOB on out-of-bounds,
 *         CATALOG_CHECKED_UNPOPULATED on (in-bounds AND sentinel_value == 0).
 */
catalog_checked_result_e catalogCheckedValidateSlot(s32 idx, s32 count, s32 sentinel_value);

/*
 * Map a result code to a stable short string for log output.
 * Returned pointer is to static storage; callers must not free or modify.
 */
const char *catalogCheckedResultName(catalog_checked_result_e r);

/*
 * Catalog health checkpoint decision (Gate 5, c3844).
 *
 * The catalog load-site helpers (catalogGetBodyFilenumByIndex, etc.) set the
 * write-only g_CatalogFailure flag on a miss and substitute a default. Nothing
 * consumed that accumulated flag, so a catalog miss after extraction was
 * logged-then-tolerated. catalogAssertHealthy() (in assetcatalog_api.c) is the
 * checkpoint consumer; this pure predicate is the single point where the
 * "escalate to a hard fail" decision lives so pd-tests can pin it without the
 * catalog runtime.
 *
 * Returns 1 (should hard-fail) only when a miss is pending AND source-only
 * enforcement is active for some asset family; otherwise 0 (loud-report and
 * tolerate in normal play so a live brick is not introduced before every
 * per-family source gate is closed).
 */
s32 catalogHealthShouldFatal(s32 failure_flag, s32 enforcement_active);

#ifdef __cplusplus
}
#endif

#endif /* PD_CATALOG_CHECKED_H */
