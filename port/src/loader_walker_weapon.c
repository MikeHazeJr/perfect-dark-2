/**
 * loader_walker_weapon.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks data/<romid>/weapons/*.pdwpn. Each .pdwpn is plain JSON; the
 * envelope carries weapon_id at top level. Step 5: also feeds the
 * heavyweight loader_pool payload (struct weapon, weaponfunc_*, ammos,
 * aim/noise/recoil settings, gunviscmds, partvis, bot_pref) by handing
 * the manifest bytes to loaderPoolParseWeaponJson.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind; (void)file_path;

    s64 weapon_id = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "weapon_id", &weapon_id);

    /* Catalog row: register only if no prior in-binary row carries this
     * id (preserves model_file + langid pairings set by
     * assetCatalogRegisterBaseGame). */
    asset_entry_t *e = NULL;
    if (assetCatalogResolve(id) == NULL) {
        e = assetCatalogRegisterWeapon(
            id, (s32)weapon_id,
            /* name: */ "",
            /* model_file: */ "",
            /* dual_wieldable: */ 0);
    } else {
        e = (asset_entry_t *)assetCatalogResolve(id);
    }

    /* Heavyweight pool payload (Step 5): always populate from the
     * envelope. parseWeapon uses weapon_id from the envelope to pick
     * the pool slot. */
    loaderPoolParseWeaponJson(manifest, manifest_len);

    return e ? 1 : -1;
}

void loaderWalkerScanWeapons(const char *tier_dir,
                              loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "weapon", "weapons", ".pdwpn", /* always_invoke: */ 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
