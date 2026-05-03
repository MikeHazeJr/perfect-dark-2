/**
 * loader_walker.c -- Catalog universality pivot Step 4 (2026-05-03).
 *
 * Top-level dispatch for the universal directory walker. Calls each
 * per-kind walker in dependency order:
 *
 *   1. meshes        (no cross-references; primitives for weapons / heads / bodies)
 *   2. animations    (no cross-references at the catalog row layer)
 *   3. weapons       (refer to meshes + animations; refs resolve at consume-time)
 *   4. heads + bodies + arenas + scenarios   (refer to meshes / scenarios)
 *   5. audio (sfx/voice/song)
 *   6. ui + fonts + lang
 *
 * Cross-references between catalog IDs do NOT need to resolve at scan
 * time; consumers (e.g. catalog managers, weapon spawn paths) resolve
 * IDs lazily via assetCatalogResolve. The dependency order above is a
 * convention for log readability, not a correctness requirement.
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"

static s32 s_walkerActive = 0;

s32 loaderWalkerIsActive(void)
{
    return s_walkerActive;
}

s32 loaderWalkerLoadAll(loader_walker_result_t *out)
{
    loader_walker_result_t local;
    memset(&local, 0, sizeof(local));
    if (out) memset(out, 0, sizeof(*out));

    const char *tier_dir = fsDataDir();
    if (!tier_dir || !tier_dir[0]) {
        sysLogPrintf(LOG_NOTE,
            "LOADER.UNIVERSAL.OK: skipped (no data dir; first-launch pre-extract)");
        if (out) *out = local;
        return 0;
    }

    /* The path returned by fsDataDir is a static buffer that subsequent
     * calls may overwrite; capture into a local before further work. */
    char data_root[FS_MAXPATH];
    snprintf(data_root, sizeof(data_root), "%s", tier_dir);

    sysLogPrintf(LOG_NOTE,
        "LOADER.UNIVERSAL.OK: walking %s/<kind>/*.pd<ext> for 13 universality kinds",
        data_root);

    /* Step 5: clear loader_pool before re-scanning so re-runs (or
     * forced re-extracts) start from a known-zero state. The four
     * pool-kind walkers (weapon/head/body/arena + animation) populate
     * typed payload via loaderPoolParse*Json during the scan; the
     * matching loaderPoolFinalize at the end flips the active flags. */
    loaderPoolReset();

    loader_walker_kind_result_t kr;

    /* Dependency tier 1: byte primitives. */
    loaderWalkerScanMeshes(data_root, &kr);
    local.meshes_registered    += kr.entries_registered;
    local.total_files_scanned  += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanAnimations(data_root, &kr);
    local.animations_registered  += kr.entries_registered;
    local.total_files_scanned    += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    /* Dependency tier 2: composite entities referencing meshes / anims. */
    loaderWalkerScanWeapons(data_root, &kr);
    local.weapons_registered    += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanHeads(data_root, &kr);
    local.heads_registered      += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanBodies(data_root, &kr);
    local.bodies_registered     += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanScenarios(data_root, &kr);
    local.scenarios_registered  += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanArenas(data_root, &kr);
    local.arenas_registered     += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    /* Dependency tier 3: audio. */
    loaderWalkerScanSfx(data_root, &kr);
    local.sfx_registered        += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanVoices(data_root, &kr);
    local.voices_registered     += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanSongs(data_root, &kr);
    local.songs_registered      += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    /* Dependency tier 4: UI / fonts / lang. */
    loaderWalkerScanUi(data_root, &kr);
    local.uis_registered        += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanFonts(data_root, &kr);
    local.fonts_registered      += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    loaderWalkerScanLangs(data_root, &kr);
    local.langs_registered      += kr.entries_registered;
    local.total_files_scanned   += kr.entries_scanned;
    local.total_envelope_failures += kr.envelope_failures;
    local.total_register_failures += kr.register_failures;

    s32 total_registered = local.weapons_registered + local.heads_registered
                        + local.bodies_registered + local.arenas_registered
                        + local.meshes_registered + local.animations_registered
                        + local.sfx_registered + local.voices_registered
                        + local.songs_registered + local.scenarios_registered
                        + local.uis_registered + local.fonts_registered
                        + local.langs_registered;

    if (total_registered > 0) {
        s_walkerActive = 1;
    }

    /* Step 5: seed default aim/noise sentinels and flip the per-kind
     * active flags so catalog managers route through the populated
     * loader_pool slots. */
    loaderPoolFinalize();

    sysLogPrintf(LOG_NOTE,
        "LOADER.UNIVERSAL.SUMMARY: scanned=%d registered=%d "
        "envelope_failures=%d register_failures=%d active=%d",
        local.total_files_scanned, total_registered,
        local.total_envelope_failures, local.total_register_failures,
        s_walkerActive);
    sysLogPrintf(LOG_NOTE,
        "LOADER.UNIVERSAL.SUMMARY: per-kind weapons=%d heads=%d bodies=%d "
        "arenas=%d meshes=%d anims=%d sfx=%d voices=%d songs=%d "
        "scenarios=%d ui=%d fonts=%d lang=%d",
        local.weapons_registered, local.heads_registered,
        local.bodies_registered, local.arenas_registered,
        local.meshes_registered, local.animations_registered,
        local.sfx_registered, local.voices_registered,
        local.songs_registered, local.scenarios_registered,
        local.uis_registered, local.fonts_registered, local.langs_registered);

    if (out) *out = local;
    return 0;
}
