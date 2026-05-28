/**
 * loader_walker_weapon.c -- Catalog universality pivot Step 4 + 5
 * (2026-05-03).
 *
 * Walks data/<romid>/weapons/*.pdweapon. Each archive carries
 * _meta/manifest.json with weapon_id at top level. Step 5: also feeds the
 * heavyweight loader_pool payload (struct weapon, weaponfunc_*, ammos,
 * aim/noise/recoil settings, gunviscmds, partvis, bot_pref) by handing
 * the manifest bytes to loaderPoolParseWeaponJson.
 */

#include <stddef.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "constants.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"
#include "weapon_graph_runtime.h"

static SDL_mutex *s_WeaponArchiveCatalogMutex = NULL;

static s32 s_mpWeaponIdForRuntimeWeapon(s32 runtime_weapon_id)
{
    for (s32 mp_weapon_id = 0; mp_weapon_id < NUM_MPWEAPONS; mp_weapon_id++) {
        if (catalogGetMpWeaponNum(mp_weapon_id) == runtime_weapon_id) {
            return mp_weapon_id;
        }
    }
    return -1;
}

static void s_bindWeaponArchiveSource(asset_entry_t *e,
                                      const char *file_path,
                                      s32 runtime_weapon_id,
                                      s32 mp_weapon_id)
{
    if (!e) {
        return;
    }

    e->runtime_index = runtime_weapon_id;
    e->mp_index = (s16)mp_weapon_id;
    e->ext.weapon.weapon_id = mp_weapon_id;
    strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
    e->category[CATALOG_CATEGORY_LEN - 1] = '\0';
    e->bundled = 1;
    e->enabled = 1;
    e->ref_count = ASSET_REF_BUNDLED;
    catalogSetPrimaryFile(e, file_path);
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 runtime_weapon_id64 = 0;
    loaderWalkerEnvelopeInt(manifest, manifest_len, "weapon_id", &runtime_weapon_id64);
    s32 runtime_weapon_id = (s32)runtime_weapon_id64;
    s32 mp_weapon_id = s_mpWeaponIdForRuntimeWeapon(runtime_weapon_id);

    asset_entry_t *e = NULL;
    if (s_WeaponArchiveCatalogMutex) {
        SDL_LockMutex(s_WeaponArchiveCatalogMutex);
    }

    /* Catalog row: register only if no prior in-binary row carries this
     * id (preserves display names, unlock gates, and dual-wield metadata
     * set by assetCatalogRegisterBaseGame). The public archive manifest
     * stores runtime WEAPON_* ids, while ext.weapon.weapon_id remains the
     * MPWEAPON_* slot; keep those domains separated. */
    e = (asset_entry_t *)assetCatalogResolve(id);
    if (e == NULL) {
        e = assetCatalogRegisterWeapon(
            id, mp_weapon_id,
            /* name: */ "",
            /* model_file: */ "",
            /* dual_wieldable: */ 0);
    }
    s_bindWeaponArchiveSource(e, file_path, runtime_weapon_id, mp_weapon_id);

    if (s_WeaponArchiveCatalogMutex) {
        SDL_UnlockMutex(s_WeaponArchiveCatalogMutex);
    }

    /* Heavyweight pool payload (Step 5): always populate from the
     * envelope. parseWeapon uses weapon_id from the envelope to pick
     * the pool slot. */
    loaderPoolParseWeaponJson(manifest, manifest_len);

    {
        char full_buf[FS_MAXPATH + 1];
        char err[256];
        const char *full = fsFullPath(file_path, full_buf, sizeof(full_buf));
        err[0] = '\0';
        if (!full || weaponGraphRuntimeRegisterWeaponArchive(runtime_weapon_id,
                full, err, sizeof(err)) != 0) {
            sysLogPrintf(LOG_WARNING,
                "weapon_graph_runtime: held IR unavailable for %s (%s)",
                id ? id : "<unknown>", err[0] ? err : "archive open failed");
        }
    }

    return e ? 1 : -1;
}

void loaderWalkerScanWeapons(const char *tier_dir,
                              loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "weapon", "weapons", ".pdweapon", /* always_invoke: */ 1,
    };
    s_WeaponArchiveCatalogMutex = SDL_CreateMutex();
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
    if (s_WeaponArchiveCatalogMutex) {
        SDL_DestroyMutex(s_WeaponArchiveCatalogMutex);
        s_WeaponArchiveCatalogMutex = NULL;
    }
}
