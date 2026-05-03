/**
 * loader_walker_weapon.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/weapons/*.pdwpn and registers each as ASSET_WEAPON.
 * Each .pdwpn is plain JSON; envelope carries weapon_id at top-level.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    s64 weapon_id = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "weapon_id", &weapon_id);

    asset_entry_t *e = assetCatalogRegisterWeapon(
        id, (s32)weapon_id,
        /* name: */ "",
        /* model_file: */ "",
        /* dual_wieldable: */ 0);
    return e ? 1 : -1;
}

void loaderWalkerScanWeapons(const char *tier_dir,
                              loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "weapon", "weapons", ".pdwpn",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
