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
#include "assetcatalog_scanner.h"
#include "assetcatalog_weapon_slots.h"
#include "constants.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "system.h"
#include "weapon_graph_runtime.h"

static SDL_mutex *s_WeaponArchiveCatalogMutex = NULL;

static s32 s_selectWeaponSlots(const char *manifest, size_t manifest_len,
                               const char *id, s32 *runtime_weapon_id_out,
                               s32 *mp_weapon_id_out)
{
    s64 runtime_weapon_id64 = -1;
    s32 has_runtime = loaderWalkerEnvelopeInt(manifest, manifest_len,
        "weapon_id", &runtime_weapon_id64);
    s32 runtime_weapon_id = has_runtime ? (s32)runtime_weapon_id64 : -1;
    return assetCatalogResolveWeaponPrivateSlots(id, runtime_weapon_id,
        has_runtime, runtime_weapon_id_out, mp_weapon_id_out);
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
    e->ext.weapon.model_file[0] = '\0';
    e->ext.weapon.behavior_graph[0] = '\0';
    e->ext.weapon.primary_graph[0] = '\0';
    e->ext.weapon.secondary_graph[0] = '\0';
    e->ext.weapon.shared_context[0] = '\0';
    e->ext.weapon.settings_file[0] = '\0';
    e->ext.weapon.variables_file[0] = '\0';
    e->ext.weapon.presentation_file[0] = '\0';
    catalogSetPrimaryFile(e, file_path);
}

static void s_copyManifestMemberPath(const char *manifest, size_t manifest_len,
                                     const char *key, const char *archive_path,
                                     char *out, size_t out_n)
{
    char member[FS_MAXPATH + 1];
    char path[FS_MAXPATH + 1];

    if (!out || out_n == 0) {
        return;
    }
    out[0] = '\0';
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, key,
            member, sizeof(member)) || !member[0]) {
        return;
    }
    if (!loaderWalkerArchiveMemberPath(archive_path, member,
            path, sizeof(path))) {
        return;
    }
    strncpy(out, path, out_n - 1);
    out[out_n - 1] = '\0';
}

static void s_copyManifestMemberPathAlias(const char *manifest,
                                          size_t manifest_len,
                                          const char *key,
                                          const char *alias,
                                          const char *archive_path,
                                          char *out,
                                          size_t out_n)
{
    s_copyManifestMemberPath(manifest, manifest_len, key, archive_path,
        out, out_n);
    if (!out || out[0] || !alias) {
        return;
    }
    s_copyManifestMemberPath(manifest, manifest_len, alias, archive_path,
        out, out_n);
}

static void s_bindWeaponArchiveSourceMembers(asset_entry_t *e,
                                             const char *manifest,
                                             size_t manifest_len,
                                             const char *file_path)
{
    if (!e || !manifest || !file_path) {
        return;
    }
    s_copyManifestMemberPath(manifest, manifest_len, "model_file",
        file_path, e->ext.weapon.model_file,
        sizeof(e->ext.weapon.model_file));
    s_copyManifestMemberPath(manifest, manifest_len, "behavior_graph",
        file_path, e->ext.weapon.behavior_graph,
        sizeof(e->ext.weapon.behavior_graph));
    s_copyManifestMemberPath(manifest, manifest_len, "primary_graph",
        file_path, e->ext.weapon.primary_graph,
        sizeof(e->ext.weapon.primary_graph));
    s_copyManifestMemberPath(manifest, manifest_len, "secondary_graph",
        file_path, e->ext.weapon.secondary_graph,
        sizeof(e->ext.weapon.secondary_graph));
    s_copyManifestMemberPath(manifest, manifest_len, "shared_context",
        file_path, e->ext.weapon.shared_context,
        sizeof(e->ext.weapon.shared_context));
    s_copyManifestMemberPathAlias(manifest, manifest_len, "settings_file",
        "settings", file_path, e->ext.weapon.settings_file,
        sizeof(e->ext.weapon.settings_file));
    s_copyManifestMemberPathAlias(manifest, manifest_len, "variables_file",
        "variables", file_path, e->ext.weapon.variables_file,
        sizeof(e->ext.weapon.variables_file));
    s_copyManifestMemberPathAlias(manifest, manifest_len, "shared_context_file",
        "shared_context", file_path, e->ext.weapon.shared_context,
        sizeof(e->ext.weapon.shared_context));
    /* c3849 Wave 5f: presentation fold-in. Field-for-field parity with
     * assetcatalog_scanner.c and netdistrib.c -- keep all three in sync. */
    s_copyManifestMemberPathAlias(manifest, manifest_len, "presentation_file",
        "presentation", file_path, e->ext.weapon.presentation_file,
        sizeof(e->ext.weapon.presentation_file));
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s32 runtime_weapon_id = -1;
    s32 mp_weapon_id = -1;
    s32 slots_ok = s_selectWeaponSlots(manifest, manifest_len, id,
        &runtime_weapon_id, &mp_weapon_id);

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
    s_bindWeaponArchiveSourceMembers(e, manifest, manifest_len, file_path);

    if (s_WeaponArchiveCatalogMutex) {
        SDL_UnlockMutex(s_WeaponArchiveCatalogMutex);
    }

    /* T-ASSETS-022: nested-only animation/audio archives must exist in the
     * catalog and weapon-animation command pool before the manifest parser
     * resolves catalog IDs into the production struct weapon payload. The
     * nested public descriptors are authoritative; any corrupt member fails
     * the owning weapon closed instead of falling through to base content. */
    {
        char nested_err[256];
        nested_err[0] = '\0';
        if (assetCatalogRegisterWeaponNestedDependencies(id, file_path,
                /* bundled: */ 1, nested_err, sizeof(nested_err)) < 0) {
            sysLogPrintf(LOG_WARNING,
                "PDWEAPON.NESTED.REJECT: owner=%s source=%s error=%s",
                id ? id : "<unknown>", file_path ? file_path : "<unknown>",
                nested_err[0] ? nested_err : "nested registration failed");
            if (s_WeaponArchiveCatalogMutex) {
                SDL_LockMutex(s_WeaponArchiveCatalogMutex);
            }
            if (e) {
                e->enabled = 0;
                e->load_state = ASSET_STATE_REGISTERED;
            }
            if (s_WeaponArchiveCatalogMutex) {
                SDL_UnlockMutex(s_WeaponArchiveCatalogMutex);
            }
            return -1;
        }
    }

    /* Heavyweight pool payload (Step 5): always populate from the
     * envelope. parseWeapon uses weapon_id from the envelope to pick
     * the pool slot. */
    if (slots_ok && runtime_weapon_id >= WEAPON_CUSTOM_START) {
        loaderPoolParseWeaponJsonWithRuntimeSlot(manifest, manifest_len,
            runtime_weapon_id);
    } else {
        loaderPoolParseWeaponJson(manifest, manifest_len);
    }

    {
        char full_buf[FS_MAXPATH + 1];
        char err[256];
        const char *full = fsFullPath(file_path, full_buf, sizeof(full_buf));
        err[0] = '\0';
        if (!full || weaponGraphRuntimeRegisterWeaponArchive(runtime_weapon_id,
                full, err, sizeof(err)) != 0) {
            sysLogPrintf(LOG_WARNING,
                "weapon_graph_runtime: refusing public weapon %s because held IR is unavailable (%s)",
                id ? id : "<unknown>", err[0] ? err : "archive open failed");
            weaponGraphRuntimeClearWeapon(runtime_weapon_id);
            if (s_WeaponArchiveCatalogMutex) {
                SDL_LockMutex(s_WeaponArchiveCatalogMutex);
            }
            if (e) {
                e->enabled = 0;
                e->load_state = ASSET_STATE_REGISTERED;
            }
            if (s_WeaponArchiveCatalogMutex) {
                SDL_UnlockMutex(s_WeaponArchiveCatalogMutex);
            }
            return -1;
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
    assetCatalogResetCustomWeaponSlots();
    s_WeaponArchiveCatalogMutex = SDL_CreateMutex();
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
    if (s_WeaponArchiveCatalogMutex) {
        SDL_DestroyMutex(s_WeaponArchiveCatalogMutex);
        s_WeaponArchiveCatalogMutex = NULL;
    }
}
