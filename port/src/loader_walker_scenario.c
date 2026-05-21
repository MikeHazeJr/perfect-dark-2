/**
 * loader_walker_scenario.c -- Step 4 (2026-05-03).
 *
 * Walks scenario .pdscenario files and registers each as
 * ASSET_MAP. Per Q-1 the .pdscenario is a UNIFIED ZIP (rooms.obj plus
 * tiles/pads/setup text payloads + manifest); the manifest envelope at
 * top level carries `stagenum` and `kind` (mp / solo / firingrange / coop).
 */

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    s64 stagenum = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "stagenum", &stagenum);

    asset_entry_t *e = assetCatalogRegisterMap(
        id, (s32)stagenum,
        /* dirpath: */ "");
    return e ? 1 : -1;
}

void loaderWalkerScanScenarios(const char *tier_dir,
                                loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "scenario", "scenarios", ".pdscenario",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
