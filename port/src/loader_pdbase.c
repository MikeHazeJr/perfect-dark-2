/*
 * port/src/loader_pdbase.c -- S484 F10: .pdbase loader skeleton.
 *
 * See port/include/loader_pdbase.h for the contract and
 * context/designs/catalog-full-pipeline-weapons-2026-04-27.md for the
 * full design.
 *
 * Phase 2 (F10): scaffold only. The directory scan walks the
 * filesystem and counts archives but does NOT decode them. The
 * catalog manager continues to source data from g_Weapons[] (parity).
 * F11+ implements actual archive decode + manager population.
 */

#include <ultra64.h>
#include <stddef.h>
#include <string.h>
#include "loader_pdbase.h"
#include "system.h"

void loaderPdbaseScan(const char *dir, loader_pdbase_result_t *out)
{
	loader_pdbase_result_t local = {0};

	if (out != NULL) {
		memset(out, 0, sizeof(*out));
	}

	if (dir == NULL || dir[0] == '\0') {
		sysLogPrintf(LOG_NOTE,
			"LOADER.PDBASE.WEAPON.OK: scan skipped (no dir specified)");
		return;
	}

	/* Phase 2 (F10): scaffold only. The directory enumeration is not
	 * implemented yet; the counters stay at zero so the integration
	 * path is exercised without behavior change. F11+ replaces this
	 * with actual fs enumeration of *.pdbase files plus archive
	 * open / manifest parse / record register. Missing-dir is treated
	 * as "no archives available" (silent NULL result, not an error).
	 */
	sysLogPrintf(LOG_NOTE,
		"LOADER.PDBASE.WEAPON.OK: dir=%s archives=%d weapons=%d "
		"scan_fail=%d resolve_fail=%d field_unknown=%d",
		dir, local.archives_scanned, local.weapons_registered,
		local.scan_failures, local.resolve_failures, local.field_unknown);

	if (out != NULL) {
		*out = local;
	}
}

s32 loaderPdbaseBuildWeaponManager(void)
{
	/* Phase 2 (F10): no records registered with non-empty pdbase_path
	 * yet, so the manager build is a no-op. F11+ iterates ASSET_WEAPON
	 * catalog rows, decodes each weapon's .pdbase record, and registers
	 * the typed weapon_data_t with the manager. */
	sysLogPrintf(LOG_NOTE,
		"LOADER.PDBASE.WEAPON.OK: build skipped (no .pdbase records registered)");
	return 0;
}
