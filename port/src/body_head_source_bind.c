#include "body_head_source_bind.h"
#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "assetcatalog_model_slots.h"
#include "assetprovider.h"
#include "constants.h"
#include "fs.h"
#include "loader_walker_common.h"
#include "modarchive.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s32 reject(char *error, size_t capacity, const char *reason, const char *id)
{
    if (error && capacity) snprintf(error, capacity, "%s: %s", reason, id ? id : "");
    return 0;
}

/* Native provenance is not a unique catalog identity: for example CamSpy's
 * body and weapon hi/lo meshes share FILE_CEYESPY. Only an established mapping
 * consumed by catalog ID may alias; private and filenum-only routes may not. */
static s32 establishedNativeAlias(const asset_entry_t *entry, s32 filenum,
    s32 source_by_catalog_id)
{
    return source_by_catalog_id && entry && entry->occupied && entry->enabled &&
        entry->type == ASSET_MODEL && entry->source_filenum == filenum &&
        filenum > 0 && filenum < assetCatalogModelPrivateSourceFilenum(MODEL_CUSTOM_START);
}

static s32 meshBridgeHasConflictingOwner(const char *id, s32 filenum)
{
    for (s32 i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const asset_entry_t *row = assetCatalogGetByIndex(i);
        if (row && row->occupied && row->type == ASSET_MODEL &&
                row->source_filenum == filenum && strcmp(row->id, id)) return 1;
    }
    return 0;
}

s32 bodyHeadSourcePrepareMesh(const char *archive_path, const char *expected_id,
    s32 source_by_catalog_id, body_head_mesh_binding_t *out, char *error, size_t error_capacity)
{
    body_head_mesh_binding_t candidate = {0};
    ini_section_t *ini = NULL;
    void *bytes = NULL;
    char *text = NULL, *metadata = NULL;
    const asset_entry_t *existing;
    const char *id, *alias, *geometry = NULL;
    u32 size = 0, text_size = 0, metadata_size = 0;
    s32 ok = 0;
    if (error && error_capacity) error[0] = 0;
    if (!out || !archive_path || !archive_path[0] ||
            assetArchiveTypeForPath(archive_path) != ASSET_MODEL ||
            assetPathHasParentTraversal(archive_path))
        return reject(error, error_capacity, "invalid typed body/head mesh path", archive_path);
    bytes = fsFileLoad(archive_path, &size);
    ini = calloc(1, sizeof(*ini));
    if (!bytes || !size || !ini || assetArchiveValidateBytes(bytes, size, archive_path,
            ASSET_ARCHIVE_VALIDATE_RELEASE, error, error_capacity) != 0) goto done;
    text = modArchiveExtractMemAlloc(bytes, size, "mesh.ini", &text_size);
    if (!bodyHeadSourceReadIniBytes(text, text_size, "model", ini, error, error_capacity)) goto done;
    id = iniGet(ini, "catalog_id", NULL);
    alias = iniGet(ini, "id", NULL);
    if ((id && alias && strcmp(id, alias)) || (!id && !(id = alias)) ||
            !bodyHeadSourceCatalogIdValid(id) ||
            (expected_id && expected_id[0] && strcmp(id, expected_id)) ||
            strcmp(iniGet(ini, "kind", ""), "mesh")) {
        reject(error, error_capacity, "public mesh identity/type mismatch", expected_id);
        goto done;
    }
    for (size_t i = 0; i < loaderWalkerMeshPublicGeometryKeyCount(); ++i) {
        const char *value = iniGet(ini, loaderWalkerMeshPublicGeometryKey(i), NULL);
        if (!value) continue;
        if (!value[0] || (geometry && strcmp(geometry, value))) {
            reject(error, error_capacity, "conflicting/empty public mesh geometry", id);
            goto done;
        }
        geometry = value;
    }
    if (!geometry) geometry = "model.obj";
    existing = assetCatalogResolveAny(id);
    if (existing && (!existing->occupied || !existing->enabled || existing->type != ASSET_MODEL)) {
        reject(error, error_capacity, "mesh catalog identity collision", id);
        goto done;
    }
    /* Current on-disk bytes cannot prove equality with a resident payload.
     * This registrar does not own an immutable loaded-source fingerprint. */
    if (existing && !loaderWalkerMeshSourceChangeAllowed(existing->load_state,
            existing->bundled, existing->ref_count, existing->loaded_data != NULL, existing->payload_kind)) {
        reject(error, error_capacity, "resident mesh must be retired before source admission", id);
        goto done;
    }
    if (existing && existing->source.primary.provider == fileProvider() &&
            !loaderWalkerMeshSourceMatchesEntry(existing, archive_path, bytes, size,
                error, error_capacity)) goto done;
    metadata = modArchiveExtractMemAlloc(bytes, size, ASSET_ARCHIVE_META_MANIFEST_PATH, &metadata_size);
    if (!loaderWalkerMeshSourcePlanManifest(metadata ? metadata : "{}",
            metadata ? metadata_size : 2, id, archive_path, geometry,
            existing ? existing->source_filenum : -1, &candidate.plan,
            error, error_capacity)) goto done;
    if (fsFileSize(candidate.plan.source_path) <= 0) {
        reject(error, error_capacity, "selected public mesh geometry is missing", id);
        goto done;
    }
    if (candidate.plan.source_filenum > 65535) {
        reject(error, error_capacity, "native mesh source bridge exceeds u16", id);
        goto done;
    }
    if (candidate.plan.source_filenum > 0) {
        if (!establishedNativeAlias(existing, candidate.plan.source_filenum, source_by_catalog_id) &&
                meshBridgeHasConflictingOwner(id, candidate.plan.source_filenum)) {
            reject(error, error_capacity, "mesh source bridge belongs to another catalog ID", id);
            goto done;
        }
    }
    strcpy(candidate.id, id);
    candidate.present = 1;
    candidate.source_by_catalog_id = !!source_by_catalog_id;
    *out = candidate;
    ok = 1;
done:
    if (!ok && error && error_capacity && !error[0])
        reject(error, error_capacity, "could not prepare body/head mesh source", archive_path);
    free(metadata);
    free(text);
    free(ini);
    if (bytes) sysMemFree(bytes);
    return ok;
}

s32 bodyHeadSourceBindMesh(body_head_mesh_binding_t *binding, s32 bundled,
    s32 temporary, s32 *out_filenum, char *error, size_t error_capacity)
{
    asset_entry_t *entry;
    s32 runtime_slot = -1, created = 0;
    if (!binding || !binding->present || !out_filenum)
        return reject(error, error_capacity, "invalid prepared mesh binding", "");
    entry = assetCatalogGetMutable(binding->id);
    if (entry && entry->type != ASSET_MODEL)
        return reject(error, error_capacity, "mesh type changed during binding", binding->id);
    if (entry && !loaderWalkerMeshSourceChangeAllowed(entry->load_state,
            entry->bundled, entry->ref_count, entry->loaded_data != NULL, entry->payload_kind)) {
        return reject(error, error_capacity, "resident mesh must be retired before source admission", binding->id);
    }
    if (binding->plan.source_filenum <= 0) {
        runtime_slot = assetCatalogResolveModelPrivateSlot(binding->id);
        binding->plan.source_filenum = assetCatalogModelPrivateSourceFilenum(runtime_slot);
        if (runtime_slot < 0 || binding->plan.source_filenum <= 0 || binding->plan.source_filenum > 65535)
            return reject(error, error_capacity, "no native model bridge for declared mesh", binding->id);
        if (entry && entry->runtime_index >= 0 && entry->runtime_index != runtime_slot)
            return reject(error, error_capacity, "model slot conflicts with existing owner", binding->id);
    }
    if (!establishedNativeAlias(entry, binding->plan.source_filenum, binding->source_by_catalog_id) &&
            meshBridgeHasConflictingOwner(binding->id, binding->plan.source_filenum))
        return reject(error, error_capacity, "native model bridge ownership collision", binding->id);
    if (!entry) { entry = assetCatalogRegister(binding->id, ASSET_MODEL); created = 1; }
    if (!entry || !loaderWalkerBindMeshSource(entry, &binding->plan, 1, error, error_capacity)) return 0;
    if (entry->runtime_index == -1) entry->runtime_index = runtime_slot >= 0
        ? runtime_slot : -binding->plan.source_filenum;
    if (!entry->descriptor_path[0] && !assetPathJoinChecked(entry->descriptor_path, sizeof(entry->descriptor_path),
            binding->plan.archive_path, "::", "mesh.ini"))
        return reject(error, error_capacity, "mesh descriptor path exceeds capacity", binding->id);
    if (created) {
        entry->temporary = !!temporary;
        if (bundled) loaderWalkerMarkBaseArchiveEntry(entry);
    }
    const char *reverse = catalogIdBySourceFilenum(ASSET_MODEL, binding->plan.source_filenum);
    if (!reverse || (!establishedNativeAlias(entry, binding->plan.source_filenum, binding->source_by_catalog_id) &&
            meshBridgeHasConflictingOwner(binding->id, binding->plan.source_filenum)))
        return reject(error, error_capacity, "mesh source bridge reverse ownership mismatch", binding->id);
    *out_filenum = binding->plan.source_filenum;
    return 1;
}
