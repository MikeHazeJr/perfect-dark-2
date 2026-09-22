#include "body_head_source_harness.h"
#include "body_head_source_bind.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_model_slots.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "assetprovider.h"
#include "assetprovider_checkpoint.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"
#include "constants.h"
#include "files.h"
#include "game/modeldef.h"
#include "game/bondgun.h"
#include "bss.h"
#include "types.h"
#include "fs.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "loader_walker_mesh_source.h"
#include "modarchive.h"
#include "modmgr.h"
#include "net/netdistrib.h"
#include "pdca_extract_transaction.h"
#include "sha256.h"
#include "smoke_harness.h"
#include "system.h"
#include "weapon_graph_archive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real client-only harness. No catalog, provider, compiler, or native-manager
 * stubs. Files stay under this smoke run's isolated save root. */
static const char k_root[] = "$S/body-head-source-smoke";

static s32 packText(mod_archive_writer_t *writer, const char *name, const char *text)
{
    return modArchiveAddFileMem(writer, name, text, (u32)strlen(text)) == MODARCHIVE_OK;
}

static s32 writeMesh(const char *id, s32 edge, const char *geometry, void **out, u32 *out_size)
{
    char path[FS_MAXPATH], full[FS_MAXPATH + 1], ini[512], obj[256], meta[512];
    snprintf(path, sizeof(path), "%s/staging.pdmesh", k_root);
    mod_archive_writer_t *writer = modArchiveBegin(fsFullPath(path, full, sizeof(full)));
    if (!writer) return 0;
    snprintf(ini, sizeof(ini), "[model]\nkind=mesh\ncatalog_id=%s\ngeometry_file=%s\n", id, geometry);
    snprintf(obj, sizeof(obj), "v 0 0 0\nv %d 0 0\nv 0 2 0\nf 1 2 3\n", edge);
    snprintf(meta, sizeof(meta), "{\"pd_kind\":\"mesh\",\"pd_schema_version\":1,\"id\":\"%s\","
        "\"geometry\":\"stale-private.obj\"}", id);
    if (!packText(writer, "mesh.ini", ini) || !packText(writer, geometry, obj) ||
            !packText(writer, "_meta/manifest.json", meta)) { modArchiveAbort(writer); return 0; }
    if (modArchiveFinish(writer) != MODARCHIVE_OK) return 0;
    *out = fsFileLoad(path, out_size);
    return *out && *out_size;
}

static s32 writeBodyHead(const char *path, const char *id, const char *mesh_id,
    const char *hand_id, s32 is_head, s32 height, s32 complete, s32 private_meta,
    s32 conflicting_shared_id, s32 mesh_edge, const char *mesh_geometry)
{
    char full[FS_MAXPATH + 1], ini[1800], meta[800];
    void *mesh = NULL, *hand = NULL;
    u32 mesh_size = 0, hand_size = 0;
    s32 ok = 0;
    mod_archive_writer_t *writer = NULL;
    const char *kind = is_head ? "head" : "body";
    if (!writeMesh(mesh_id, mesh_edge, mesh_geometry, &mesh, &mesh_size) ||
            (hand_id && !writeMesh(conflicting_shared_id ? mesh_id : hand_id, 11, "authored.obj", &hand, &hand_size))) goto done;
    int length = snprintf(ini, sizeof(ini), "[%s]\ncatalog_id=%s\nismale=0\nunk00_01=%d\n"
        "type=HEADBODYTYPE_MAIAN\nheight=%d\nscale=0.75\nanimscale=1.25\n"
        "rig_class=smoke_neck\nmesh_catalog_id=%s\nmesh_archive=edited_mesh.pdmesh\n",
        kind, id, complete, height, mesh_id);
    if (length <= 0 || (size_t)length >= sizeof(ini)) goto done;
    if (!is_head) length += snprintf(ini + length, sizeof(ini) - (size_t)length,
        "canvaryheight=1\ndisplay_name=Edited source body\nhand_catalog_id=%s\nhand_archive=%s\n",
        hand_id ? (conflicting_shared_id ? mesh_id : hand_id) : "", hand_id ? "edited_hand.pdmesh" : "");
    if (length <= 0 || (size_t)length >= sizeof(ini)) goto done;
    /* Identity, scalars and selected archive paths are stale. The ordinary
     * walker must still read the correct public owner and source values. */
    snprintf(meta, sizeof(meta), "{\"pd_kind\":\"%s\",\"pd_schema_version\":1,"
        "\"id\":\"smoke:stale_private_identity\",\"%snum\":3,\"ismale\":1,"
        "\"unk00_01\":0,\"canvaryheight\":0,\"type\":0,\"height\":100,"
        "\"scale\":9,\"animscale\":8,\"mesh\":17,\"hand\":18,"
        "\"mesh_archive\":\"stale.pdmesh\",\"hand_archive\":\"stale_hand.pdmesh\"}", kind, kind);
    writer = modArchiveBegin(fsFullPath(path, full, sizeof(full)));
    if (!writer || !packText(writer, is_head ? "head.ini" : "body.ini", ini) ||
            (private_meta && !packText(writer, "_meta/manifest.json", meta)) ||
            modArchiveAddFileMem(writer, "edited_mesh.pdmesh", mesh, mesh_size) != MODARCHIVE_OK ||
            (hand && modArchiveAddFileMem(writer, "edited_hand.pdmesh", hand, hand_size) != MODARCHIVE_OK)) goto done;
    ok = modArchiveFinish(writer) == MODARCHIVE_OK;
    writer = NULL;
done:
    if (writer) modArchiveAbort(writer);
    if (mesh) sysMemFree(mesh);
    if (hand) sysMemFree(hand);
    return ok;
}

static s32 nativeTriangle(const struct modeldef *model, s32 edge)
{
    if (!model || !model->rootnode) return 0;
    const struct modelnode *node = model->rootnode->child;
    for (s32 i = 0; node && i < 16; ++i, node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const struct modelrodata_dl *dl = &node->rodata->dl;
        if (!dl->vertices || dl->numvertices != 3) return 0;
        return dl->vertices[0].x == 0 && dl->vertices[0].y == 0 && dl->vertices[0].z == 0 &&
            dl->vertices[1].x == edge && dl->vertices[1].y == 0 && dl->vertices[1].z == 0 &&
            dl->vertices[2].x == 0 && dl->vertices[2].y == 2 && dl->vertices[2].z == 0;
    }
    return 0;
}

static s32 checkInstalled(const char *id, s32 is_head, const char *mesh_id,
    const char *hand_id, s32 height, s32 complete, s32 prove_compile)
{
    const asset_entry_t *entry = assetCatalogResolve(id);
    if (!entry || entry->type != (is_head ? ASSET_HEAD : ASSET_BODY) || entry->runtime_index < 0) return 0;
    s32 slot = entry->runtime_index, hand_file = 0;
    const char *owner = is_head ? catalogHeadIdByHeadnum(slot) : catalogBodyIdByBodynum(slot);
    if (!owner || strcmp(owner, id) || !catalogDepContains(id, mesh_id) ||
            catalogDepExpectedType(id, mesh_id) != ASSET_MODEL) return 0;
    if (is_head) {
        const head_data_t *native = catalogManagerGetHeadByIndex(slot);
        if (!native || strcmp(native->catalog_id, id) || native->height != height ||
                native->ismale != 0 || native->unk00_01 != complete || native->type != HEADBODYTYPE_MAIAN ||
                native->scale != 0.75f || native->animscale != 1.25f) return 0;
    } else {
        const body_data_t *native = catalogManagerGetBodyByIndex(slot);
        if (!native || strcmp(native->catalog_id, id) || native->height != height ||
                native->ismale != 0 || native->unk00_01 != complete || native->canvaryheight != 1 ||
                native->type != HEADBODYTYPE_MAIAN || native->scale != 0.75f || native->animscale != 1.25f) return 0;
        hand_file = native->handfilenum;
    }
    if (hand_id) {
        char selected_id[64];
        asset_data_handle_t handle;
        if (!hand_file || !catalogGetBodyHandSourceChecked(slot, selected_id, &handle) ||
                strcmp(selected_id, hand_id) || !catalogDepContains(id, hand_id) ||
                catalogDepExpectedType(id, hand_id) != ASSET_MODEL) return 0;
        const char *path = handle.provider == fileProvider() ? fileProviderPath(handle) : NULL;
        if (!path || !strstr(path, "edited_hand.pdmesh::authored.obj")) return 0;
    }
    if (prove_compile) {
        s32 loaded = catalogLoadTypedAsset(is_head ? ASSET_HEAD : ASSET_BODY, id);
        s32 valid = loaded && nativeTriangle(catalogGetLoadedModeldef(id), 7);
        if (valid && hand_id) valid = nativeTriangle(catalogGetLoadedModeldef(hand_id), 11);
        if (loaded) catalogReleaseTypedAsset(is_head ? ASSET_HEAD : ASSET_BODY, id);
        if (!valid) return 0;
    }
    const head_data_t *head = is_head ? catalogManagerGetHeadByIndex(slot) : NULL;
    const body_data_t *body = is_head ? NULL : catalogManagerGetBodyByIndex(slot);
    return is_head ? head && head->height == height : body && body->height == height;
}

static s32 digestPrivate(const char *archive_path, char out[65])
{
    u32 size = 0, length = 0;
    void *bytes = fsFileLoad(archive_path, &size);
    void *meta = bytes ? modArchiveExtractMemAlloc(bytes, size, "_meta/manifest.json", &length) : NULL;
    u8 digest[32];
    s32 ok = meta != NULL;
    if (meta) { sha256Hash(meta, length, digest); sha256ToHex(digest, out); }
    free(meta);
    if (bytes) sysMemFree(bytes);
    return ok;
}

static s32 scannerCase(void)
{
    const char *root = "$S/body-head-source-smoke/scanner";
    const char *body_path = "$S/body-head-source-smoke/scanner/body.pdbody";
    const char *head_path = "$S/body-head-source-smoke/scanner/head.pdhead";
    char full[FS_MAXPATH + 1], before[65], after[65];
    if (!fsCreateDir(root) || !writeBodyHead(body_path, "smoke:public_body", "smoke:public_body_mesh",
            "smoke:public_hand", 0, 199, 1, 1, 0, 7, "authored.obj") ||
            !writeBodyHead(head_path, "smoke:public_head", "smoke:public_head_mesh", NULL, 1, 177, 1, 1, 0, 7, "authored.obj") ||
            assetCatalogScanExternalLayoutFolder("smoke_body_head", fsFullPath(root, full, sizeof(full))) <= 0 ||
            !checkInstalled("smoke:public_body", 0, "smoke:public_body_mesh", "smoke:public_hand", 199, 1, 1) ||
            !checkInstalled("smoke:public_head", 1, "smoke:public_head_mesh", NULL, 177, 1, 1) ||
            !digestPrivate(body_path, before)) return 0;
    const asset_entry_t *body = assetCatalogResolve("smoke:public_body");
    s32 slot = body ? body->runtime_index : -1;
    if (!writeBodyHead(body_path, "smoke:public_body", "smoke:public_body_mesh", "smoke:public_hand",
            0, 213, 0, 1, 0, 7, "authored.obj") || !digestPrivate(body_path, after) || strcmp(before, after) ||
            assetCatalogScanExternalLayoutFolder("smoke_body_head", fsFullPath(root, full, sizeof(full))) <= 0 ||
            !checkInstalled("smoke:public_body", 0, "smoke:public_body_mesh", "smoke:public_hand", 213, 0, 0)) return 0;
    body = assetCatalogResolve("smoke:public_body");
    if (!body || body->runtime_index != slot) return 0;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=scanner_edit private_sha256=%s slot=%d result=PASS", before, slot);
    return 1;
}

static s32 residentGeometryCase(void)
{
    const char *root = "$S/body-head-source-smoke/resident";
    const char *path = "$S/body-head-source-smoke/resident/body.pdbody";
    const char *id = "smoke:resident_body";
    char full[FS_MAXPATH + 1];
    if (!fsCreateDir(root) || !writeBodyHead(path, id, "smoke:resident_mesh", NULL,
            0, 199, 1, 1, 0, 7, "authored.obj") ||
            assetCatalogScanExternalLayoutFolder("smoke_resident", fsFullPath(root, full, sizeof(full))) <= 0 ||
            !catalogLoadTypedAsset(ASSET_BODY, id)) return 0;
    const struct modeldef *resident = catalogGetLoadedModeldef(id);
    s32 ok = nativeTriangle(resident, 7);
    /* Same archive path and member name, different authored vertices. Current
     * file equality cannot establish equality with this retained model. */
    if (!writeBodyHead(path, id, "smoke:resident_mesh", NULL, 0, 199, 1, 1, 0, 13, "authored.obj") ||
            assetCatalogScanExternalLayoutFolder("smoke_resident", fsFullPath(root, full, sizeof(full))) >= 0 ||
            catalogGetLoadedModeldef(id) != resident || !nativeTriangle(resident, 7)) ok = 0;
    catalogReleaseTypedAsset(ASSET_BODY, id);
    if (!ok || catalogGetLoadedModeldef(id) ||
            assetCatalogScanExternalLayoutFolder("smoke_resident", fsFullPath(root, full, sizeof(full))) <= 0 ||
            !catalogLoadTypedAsset(ASSET_BODY, id)) return 0;
    ok = nativeTriangle(catalogGetLoadedModeldef(id), 13);
    catalogReleaseTypedAsset(ASSET_BODY, id);
    if (!ok || !writeBodyHead(path, id, "smoke:resident_mesh", NULL,
            0, 199, 1, 1, 0, 17, "renamed.obj") ||
            assetCatalogScanExternalLayoutFolder("smoke_resident", fsFullPath(root, full, sizeof(full))) <= 0 ||
            !catalogLoadTypedAsset(ASSET_BODY, id)) return 0;
    const asset_entry_t *mesh = assetCatalogResolve("smoke:resident_mesh");
    const char *selected = mesh && mesh->source.primary.provider == fileProvider()
        ? fileProviderPath(mesh->source.primary) : NULL;
    ok = selected && strstr(selected, "edited_mesh.pdmesh::renamed.obj") &&
        nativeTriangle(catalogGetLoadedModeldef(id), 17);
    catalogReleaseTypedAsset(ASSET_BODY, id);
    return ok;
}

static s32 walkerCase(void)
{
    const char *root = "$S/body-head-source-smoke/walker";
    loader_walker_kind_result_t bodies = {0}, heads = {0};
    if (!fsCreateDir(root) || !fsCreateDir("$S/body-head-source-smoke/walker/bodies") ||
            !fsCreateDir("$S/body-head-source-smoke/walker/heads") ||
            !writeBodyHead("$S/body-head-source-smoke/walker/bodies/body.pdbody", "smoke:walker_body",
                "smoke:walker_body_mesh", "smoke:walker_hand", 0, 199, 1, 1, 0, 7, "authored.obj") ||
            !writeBodyHead("$S/body-head-source-smoke/walker/heads/head.pdhead", "smoke:walker_head",
                "smoke:walker_head_mesh", NULL, 1, 177, 1, 0, 0, 7, "authored.obj")) return 0;
    loaderWalkerScanBodies(root, &bodies);
    loaderWalkerScanHeads(root, &heads);
    /* Boot runs this later pass after the specialized loaders. The stale
     * private body ID/paths and metadata-free head must remain untouched. */
    loader_walker_metadata_result_t metadata = {0};
    loaderWalkerScanMetadataFamilies(root, &metadata);
    s32 ok = metadata.entries_scanned == 0 && metadata.register_failures == 0 &&
        metadata.envelope_failures == 0 &&
        bodies.entries_registered == 1 && bodies.envelope_failures == 0 && bodies.register_failures == 0 &&
        heads.entries_registered == 1 && heads.envelope_failures == 0 && heads.register_failures == 0 &&
        checkInstalled("smoke:walker_body", 0, "smoke:walker_body_mesh", "smoke:walker_hand", 199, 1, 1) &&
        checkInstalled("smoke:walker_head", 1, "smoke:walker_head_mesh", NULL, 177, 1, 1) &&
        !assetCatalogResolve("smoke:stale_private_identity");
    if (ok) sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=late_metadata "
        "body_source=preserved head_source=preserved stale_private=ignored result=PASS");
    return ok;
}

static void put16(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }
static void put32(u8 *p, u32 n) { for (s32 i = 0; i < 4; ++i) p[i] = (u8)(n >> (i * 8)); }

static s32 writeBytes(const char *path, const void *bytes, size_t size)
{
    char full[FS_MAXPATH + 1];
    FILE *file = fopen(fsFullPath(path, full, sizeof(full)), "wb");
    if (!file) return 0;
    s32 ok = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

/* Real loose-component PDCA framing, with the readable descriptor alongside
 * its nested standard geometry archives and deliberately stale private echo. */
static s32 writeReceivedPdca(const char *path, const char *archive_path, s32 is_head)
{
    const char *names[] = {is_head ? "head.ini" : "body.ini", "edited_mesh.pdmesh",
        "_meta/manifest.json", "edited_hand.pdmesh"};
    void *members[4] = {0};
    u32 sizes[4] = {0}, size = 0;
    void *archive = fsFileLoad(archive_path, &size);
    size_t total = 6;
    u8 *packet = NULL;
    s32 ok = 0, count = is_head ? 3 : 4;
    if (!archive) goto done;
    for (s32 i = 0; i < count; ++i) {
        members[i] = modArchiveExtractMemAlloc(archive, size, names[i], &sizes[i]);
        if (!members[i]) goto done;
        total += 2 + strlen(names[i]) + 1 + 4 + sizes[i];
    }
    packet = malloc(total);
    if (!packet) goto done;
    put32(packet, PDCA_ARCHIVE_MAGIC); put16(packet + 4, (u16)count);
    u8 *next = packet + 6;
    for (s32 i = 0; i < count; ++i) {
        u16 name_size = (u16)(strlen(names[i]) + 1);
        put16(next, name_size); next += 2;
        memcpy(next, names[i], name_size); next += name_size;
        put32(next, sizes[i]); next += 4;
        memcpy(next, members[i], sizes[i]); next += sizes[i];
    }
    ok = writeBytes(path, packet, total);
done:
    free(packet);
    for (s32 i = 0; i < 4; ++i) free(members[i]);
    if (archive) sysMemFree(archive);
    return ok;
}

static s32 receiveOne(const char *packet, const char *id, s32 expected_success)
{
    const char *list = "$S/body-head-source-smoke/receive.txt";
    char text[FS_MAXPATH + CATALOG_ID_LEN + 64];
    int size = snprintf(text, sizeof(text), "%s|%s|body_head_source_smoke|0\n", packet, id);
    distrib_client_status_t status;
    if (size <= 0 || (size_t)size >= sizeof(text) || !writeBytes(list, text, (size_t)size) ||
            netDistribDebugReceivePdcaListForSmoke(list) != 1) return 0;
    netDistribClientGetStatus(&status);
    return expected_success ? status.state == DISTRIB_CSTATE_DONE && status.received_count == 1 :
        status.state == DISTRIB_CSTATE_ERROR && status.received_count == 0;
}

static s32 digestFile(const char *path, char hex[65])
{
    u32 size = 0;
    void *bytes = fsFileLoad(path, &size);
    u8 digest[32];
    if (!bytes) return 0;
    sha256Hash(bytes, size, digest); sha256ToHex(digest, hex);
    sysMemFree(bytes);
    return 1;
}

static s32 receivedCase(void)
{
    const char *body_zip = "$S/body-head-source-smoke/received.pdbody";
    const char *head_zip = "$S/body-head-source-smoke/received.pdhead";
    const char *packet = "$S/body-head-source-smoke/received.pdca";
    const char *id = "smoke:received_body";
    char descriptor[FS_MAXPATH], original[65], restored[65], before_private[65], after_private[65];
    if (!writeBodyHead(body_zip, id, "smoke:received_mesh", "smoke:received_hand",
            0, 199, 1, 1, 0, 7, "authored.obj") || !digestPrivate(body_zip, before_private) ||
            !writeReceivedPdca(packet, body_zip, 0) || !receiveOne(packet, id, 1) ||
            !checkInstalled(id, 0, "smoke:received_mesh", "smoke:received_hand", 199, 1, 1)) return 0;
    const asset_entry_t *entry = assetCatalogResolve(id);
    s32 slot = entry ? entry->runtime_index : -1;
    if (!entry) return 0;
    strcpy(descriptor, entry->descriptor_path);
    if (!writeBodyHead(body_zip, id, "smoke:received_mesh", "smoke:received_hand",
            0, 211, 0, 1, 0, 7, "authored.obj") || !digestPrivate(body_zip, after_private) ||
            strcmp(before_private, after_private) || !writeReceivedPdca(packet, body_zip, 0) ||
            !receiveOne(packet, id, 1) || !checkInstalled(id, 0, "smoke:received_mesh",
                "smoke:received_hand", 211, 0, 1) || !digestFile(descriptor, original)) return 0;
    entry = assetCatalogResolve(id);
    if (!entry || entry->runtime_index != slot || !catalogLoadTypedAsset(ASSET_BODY, id)) return 0;
    const struct modeldef *resident = catalogGetLoadedModeldef(id);
    s32 ok = writeBodyHead(body_zip, id, "smoke:received_mesh", "smoke:received_hand",
        0, 222, 0, 1, 0, 13, "authored.obj") && writeReceivedPdca(packet, body_zip, 0) &&
        receiveOne(packet, id, 0) && digestFile(descriptor, restored) && !strcmp(original, restored) &&
        catalogGetLoadedModeldef(id) == resident && nativeTriangle(resident, 7) &&
        checkInstalled(id, 0, "smoke:received_mesh", "smoke:received_hand", 211, 0, 0);
    catalogReleaseTypedAsset(ASSET_BODY, id);
    if (!ok || !receiveOne(packet, id, 1) || !catalogLoadTypedAsset(ASSET_BODY, id)) return 0;
    ok = nativeTriangle(catalogGetLoadedModeldef(id), 13);
    catalogReleaseTypedAsset(ASSET_BODY, id);
    entry = assetCatalogResolve(id);
    if (!ok || !entry || entry->runtime_index != slot ||
            !checkInstalled(id, 0, "smoke:received_mesh", "smoke:received_hand", 222, 0, 0)) return 0;
    return writeBodyHead(head_zip, "smoke:received_head", "smoke:received_head_mesh", NULL,
            1, 177, 1, 1, 0, 7, "authored.obj") && writeReceivedPdca(packet, head_zip, 1) &&
        receiveOne(packet, "smoke:received_head", 1) &&
        checkInstalled("smoke:received_head", 1, "smoke:received_head_mesh", NULL, 177, 1, 1);
}

static s32 rollbackCase(void)
{
    const char *root = "$S/body-head-source-smoke/rollback";
    const char *path = "$S/body-head-source-smoke/rollback/body.pdbody";
    char full[FS_MAXPATH + 1];
    void *slots = assetCatalogSnapshotCustomModelSlots();
    file_provider_checkpoint_t before = {0}, after = {0};
    s32 bodies = loaderPoolGetBodiesRegistered(), heads = loaderPoolGetHeadsRegistered();
    s32 edges = catalogDepCount(), probe = -1, ok = 0;
    if (!slots || !fileProviderCheckpointCreate(&before)) goto done;
    probe = assetCatalogResolveModelPrivateSlot("smoke:rollback_probe_before");
    if (probe < 0 || !assetCatalogRestoreCustomModelSlots(slots) || !fsCreateDir(root) ||
            !writeBodyHead(path, "smoke:rollback_body", "smoke:rollback_shared", "smoke:unused",
                0, 199, 1, 1, 1, 7, "authored.obj")) goto done;
    /* Both IDs are absent during preflight. Body mesh publishes first; the
     * divergent same-ID hand then fails and must undo that acquired child. */
    if (assetCatalogScanExternalLayoutFolder("smoke_rollback", fsFullPath(root, full, sizeof(full))) >= 0 ||
            assetCatalogResolve("smoke:rollback_body") || assetCatalogResolve("smoke:rollback_shared") ||
            catalogDepCount() != edges || loaderPoolGetBodiesRegistered() != bodies ||
            loaderPoolGetHeadsRegistered() != heads || !fileProviderCheckpointCreate(&after) ||
            memcmp(&before, &after, sizeof(before))) goto done;
    ok = assetCatalogResolveModelPrivateSlot("smoke:rollback_probe_after") == probe &&
        checkInstalled("smoke:public_body", 0, "smoke:public_body_mesh", "smoke:public_hand", 213, 0, 0);
done:
    if (slots) { assetCatalogRestoreCustomModelSlots(slots); assetCatalogDestroyCustomModelSlotSnapshot(slots); }
    return ok;
}

/* The historical MP selector slot is distinct from the native head index. */
static s32 baseHeadIdentityCase(void)
{
    const char *id = "base:sp_head_21";
    s32 owners = 0;
    if (assetCatalogResolveAny("base:head_75")) return 0;
    for (s32 i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        if (!entry || !entry->occupied || entry->type != ASSET_HEAD ||
                (entry->runtime_index != 21 && entry->ext.head.headnum != 21)) continue;
        if (!entry->enabled || strcmp(entry->id, id) || entry->runtime_index != 21 ||
                entry->ext.head.headnum != 21 || entry->ext.head.requirefeature != 0) return 0;
        ++owners;
    }
    const struct mphead *selected = modmgrGetTotalHeads() > 75 ? modmgrGetHead(75) : NULL;
    const char *native_id = catalogHeadIdByHeadnum(21);
    const char *selector_id = catalogMpHeadId(75);
    const asset_entry_t *entry = assetCatalogResolve(id);
    const head_data_t *native = catalogManagerGetHeadByIndex(21);
    const head_data_t *source = loaderPoolGetHead(21);
    if (owners != 1 || !selected || selected->headnum != 21 || selected->requirefeature != 0 ||
            !native_id || strcmp(native_id, id) || !selector_id || strcmp(selector_id, id) ||
            !entry || entry->mp_index != 75 || !native || strcmp(native->catalog_id, id) ||
            native->type != HEADBODYTYPE_MAIAN || catalogGetHeadType(21) != HEADBODYTYPE_MAIAN ||
            !source || strcmp(source->catalog_id, id) || source->type != HEADBODYTYPE_MAIAN) return 0;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=base_head_identity "
        "id=%s native=21 mp=75 owners=1 type=MAIAN requirefeature=0 result=PASS", id);
    return 1;
}

/* Native provenance aliases must not redirect a catalog-selected body source. */
static s32 nativeMeshAliasCase(void)
{
    const char *mesh_id = "smoke:native_alias_selected";
    const char *body_id = "smoke:native_alias_body";
    const char *private_owner_id = "smoke:native_alias_private_owner";
    const char *late_owner_id = "smoke:native_alias_late_owner";
    const char *path = "$S/body-head-source-smoke/native-alias/body.pdbody";
    const char *nested = "$S/body-head-source-smoke/native-alias/body.pdbody::edited_mesh.pdmesh";
    const asset_entry_t *base = assetCatalogResolve("base:sp_body_108");
    const asset_entry_t *base_mesh = assetCatalogResolve("base:model_ceyespy");
    const body_data_t *native = catalogManagerGetBodyByIndex(108);
    body_head_mesh_binding_t binding = {0};
    char error[256] = {0};
    const char *checkpoint = "base_source_preflight";
    void *slots = assetCatalogSnapshotCustomModelSlots();
    s32 ok = 0;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: native_alias_preflight "
        "slots=%d body=%d mesh=%d native=%d body_file=%d mesh_file=%d native_file=%d "
        "body_source=%s mesh_source=%s dependency=%d",
        slots != NULL, base != NULL, base_mesh != NULL, native != NULL,
        base ? base->source_filenum : -1, base_mesh ? base_mesh->source_filenum : -1,
        native ? native->filenum : -1,
        base && base->source.primary.provider == fileProvider() ? fileProviderPath(base->source.primary) : "(not file)",
        base_mesh && base_mesh->source.primary.provider == fileProvider() ? fileProviderPath(base_mesh->source.primary) : "(not file)",
        base && base_mesh ? catalogDepContains(base->id, base_mesh->id) : 0);
    if (!slots || !base || !base_mesh || !native || base->source_filenum != FILE_CEYESPY ||
            native->filenum != FILE_CEYESPY || base_mesh->source_filenum != FILE_CEYESPY ||
            base->source.primary.provider != fileProvider() ||
            memcmp(&base->source.primary, &base_mesh->source.primary, sizeof(base->source.primary)) ||
            !catalogDepContains(base->id, base_mesh->id)) goto done;
    checkpoint = "fixture_directory";
    if (!fsCreateDir("$S/body-head-source-smoke/native-alias")) goto done;
    checkpoint = "fixture_archive";
    if (!writeBodyHead(path, body_id, mesh_id, NULL, 0, 199, 1, 1, 0, 7, "authored.obj")) goto done;
    checkpoint = "private_slot";
    /* Reserve the conflicting private owner first, independently of directory
     * enumeration, then establish a selected model with native provenance. */
    s32 private_slot = assetCatalogResolveModelPrivateSlot(private_owner_id);
    s32 private_file = assetCatalogModelPrivateSourceFilenum(private_slot);
    asset_entry_t *owner = assetCatalogRegister(private_owner_id, ASSET_MODEL);
    if (!owner || private_file <= 0) goto done;
    owner->source_filenum = private_file;
    owner->enabled = 1;
    checkpoint = "selected_registration";
    asset_entry_t *selected = assetCatalogRegister(mesh_id, ASSET_MODEL);
    if (!selected) goto done;
    selected->source_filenum = FILE_CEYESPY;
    selected->enabled = 1;
    checkpoint = "selected_body_admission";
    const char *reverse = catalogIdBySourceFilenum(ASSET_MODEL, FILE_CEYESPY);
    if (!reverse || !strcmp(reverse, mesh_id) ||
            !assetCatalogRegisterBodyHeadArchive(path, body_id, 0, -1, 0, error, sizeof(error)) ||
            !checkInstalled(body_id, 0, mesh_id, NULL, 199, 1, 1)) goto done;
    checkpoint = "selected_preview";
    /* The first reverse alias is different, yet the actual body compiler
     * consumed our selected triangle, not CamSpy's unrelated geometry. */
    selected = assetCatalogGetMutable(mesh_id);
    if (!selected) goto done;
    struct modeldef *preview = modeldefLoadFromHandle(selected->source.primary,
        FILE_CEYESPY, NULL, 0, NULL);
    s32 preview_selected = nativeTriangle(preview, 7);
    if (preview) catalogReleaseStageAsset(ASSET_MODEL, mesh_id);
    if (!preview_selected) goto done;
    checkpoint = "missing_preview";
    selected = assetCatalogGetMutable(mesh_id);
    if (!selected) goto done;
    asset_data_handle_t missing = fileProviderHandle("$S/body-head-source-smoke/native-alias/missing.obj");
    asset_data_handle_t no_override = {0};
    catalogSetOverride(selected, missing);
    preview = modeldefLoadFromHandle(missing, FILE_CEYESPY, NULL, 0, NULL);
    selected = assetCatalogGetMutable(mesh_id);
    if (selected) catalogSetOverride(selected, no_override);
    if (preview) { catalogReleaseStageAsset(ASSET_MODEL, mesh_id); goto done; }
    if (!selected) goto done;
    checkpoint = "native_raw_bridge_collision";
    if (bodyHeadSourcePrepareMesh(nested, mesh_id, 0, &binding, error, sizeof(error)) ||
            !strstr(error, "bridge belongs to another catalog ID")) goto done;
    selected = assetCatalogGetMutable(mesh_id);
    if (!selected) goto done;
    selected->source_filenum = private_file;
    checkpoint = "private_collision_first_owner";
    for (s32 direct = 0; direct <= 1; ++direct) {
        if (bodyHeadSourcePrepareMesh(nested, mesh_id, direct, &binding, error, sizeof(error)) ||
                !strstr(error, "bridge belongs to another catalog ID")) goto done;
    }
    /* Now the selected ID is first. A conflicting later row must also reject,
     * even though a first-owner reverse lookup alone returns the selected ID. */
    owner = assetCatalogGetMutable(private_owner_id);
    if (!owner) goto done;
    owner->source_filenum = 0;
    checkpoint = "private_collision_late_owner";
    asset_entry_t *late_owner = assetCatalogRegister(late_owner_id, ASSET_MODEL);
    if (!late_owner) goto done;
    late_owner->source_filenum = private_file;
    late_owner->enabled = 1;
    reverse = catalogIdBySourceFilenum(ASSET_MODEL, private_file);
    if (!reverse || strcmp(reverse, mesh_id)) goto done;
    for (s32 direct = 0; direct <= 1; ++direct) {
        if (bodyHeadSourcePrepareMesh(nested, mesh_id, direct, &binding, error, sizeof(error)) ||
                !strstr(error, "bridge belongs to another catalog ID")) goto done;
    }
    selected = assetCatalogGetMutable(mesh_id);
    if (!selected) goto done;
    selected->source_filenum = FILE_CEYESPY;
    ok = 1;
    if (ok) sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=native_alias "
        "base_body=base:sp_body_108 filenum=%d selected_geometry=1 preview_geometry=1 filenum_collision=rejected "
        "private_collision=rejected both_orders=1 missing_source=rejected result=PASS", FILE_CEYESPY);
done:
    if (!ok) sysLogPrintf(LOG_WARNING,
        "ASSET.SOURCE.BODYHEAD: native_alias_failed checkpoint=%s error=%s", checkpoint, error);
    if (assetCatalogResolveAny(body_id)) assetCatalogUnregister(body_id);
    if (assetCatalogResolveAny(mesh_id)) assetCatalogUnregister(mesh_id);
    if (assetCatalogResolveAny(private_owner_id)) assetCatalogUnregister(private_owner_id);
    if (assetCatalogResolveAny(late_owner_id)) assetCatalogUnregister(late_owner_id);
    if (slots) { assetCatalogRestoreCustomModelSlots(slots); assetCatalogDestroyCustomModelSlotSnapshot(slots); }
    return ok;
}

/* Exercise the production queue and decoder, including the fists target. */
static s32 handSourceIdentityCase(void)
{
    const char *body_id = "smoke:hand_alias_body";
    const char *mesh_id = "smoke:hand_alias_body_mesh";
    const char *hand_id = "smoke:hand_alias_selected";
    const char *other_id = "smoke:hand_alias_same_source";
    const char *path = "$S/body-head-source-smoke/hand-alias/body.pdbody";
    const char *edited_path = "$S/body-head-source-smoke/hand-alias/edited.obj";
    const char *edited = "v 0 0 0\nv 13 0 0\nv 0 2 0\nf 1 2 3\n";
    char error[256], base_id[64];
    asset_data_handle_t base_handle;
    const asset_entry_t *base = assetCatalogResolve("base:dark_combat");
    struct player *saved = g_Vars.currentplayer;
    struct player *player = calloc(1, sizeof(*player));
    void *slots = assetCatalogSnapshotCustomModelSlots();
    s32 ok = 0, slot = -1;
    if (!player || !slots || !base ||
            !catalogGetBodyHandSourceChecked(base->runtime_index, base_id, &base_handle) ||
            strcmp(base_id, "base:model_combathandslod_hand") ||
            !fsCreateDir("$S/body-head-source-smoke/hand-alias") ||
            !writeBodyHead(path, body_id, mesh_id, hand_id, 0, 199, 1, 1, 0, 7, "authored.obj")) goto done;
    asset_entry_t *selected = assetCatalogRegister(hand_id, ASSET_MODEL);
    if (!selected) goto done;
    selected->source_filenum = FILE_GCOMBATHANDSLOD;
    selected->enabled = 1;
    const char *reverse = catalogIdBySourceFilenum(ASSET_MODEL, FILE_GCOMBATHANDSLOD);
    if (!reverse || !strcmp(reverse, hand_id) ||
            !assetCatalogRegisterBodyHeadArchive(path, body_id, 0, -1, 0, error, sizeof(error)) ||
            !checkInstalled(body_id, 0, mesh_id, hand_id, 199, 1, 0)) goto done;
    const asset_entry_t *body = assetCatalogResolve(body_id);
    if (!body) goto done;
    slot = body->runtime_index;
    asset_data_handle_t selected_handle = catalogEffectiveHandle(assetCatalogResolve(hand_id));
    asset_entry_t *other = assetCatalogRegister(other_id, ASSET_MODEL);
    if (!other) goto done;
    other->source_filenum = FILE_GCOMBATHANDSLOD;
    other->enabled = 1;
    catalogSetPrimary(other, selected_handle);
    g_Vars.currentplayer = player;
    if (!bgunQueueBodyHandModelLoad(player, slot, &player->gunctrl.handmodeldef, NULL, NULL)) goto done;
    bgunTickGunLoad();
    if (!nativeTriangle(player->gunctrl.handmodeldef, 11) ||
            strcmp(player->gunctrl.loadcatalogid, hand_id) ||
            !bgunBodyHandSourceIsCurrent(player, slot)) goto done;
    catalogReleaseStageAsset(ASSET_MODEL, hand_id);
    player->gunctrl.handmodeldef = NULL;
    /* Same provider handle and native number, different catalog identity.
     * The real pool/cache/queue must not reverse-resolve back to the first ID. */
    body_data_t changed_body = *catalogManagerGetBodyByIndex(slot);
    strcpy(changed_body.hand_catalog_id, other_id);
    if (!loaderPoolInstallPublicBody(&changed_body) || bgunBodyHandSourceIsCurrent(player, slot) ||
            !bgunQueueBodyHandModelLoad(player, slot, &player->gunctrl.gunmodeldef, NULL, NULL)) goto done;
    bgunTickGunLoad();
    if (!nativeTriangle(player->gunctrl.gunmodeldef, 11) ||
            strcmp(player->gunctrl.loadcatalogid, other_id) ||
            catalogGetLoadedModeldef(other_id) != player->gunctrl.gunmodeldef) goto done;
    catalogReleaseStageAsset(ASSET_MODEL, other_id);
    player->gunctrl.gunmodeldef = NULL;
    strcpy(changed_body.hand_catalog_id, hand_id);
    if (!loaderPoolInstallPublicBody(&changed_body)) goto done;
    FILE *file = fsFileOpenWrite(edited_path);
    if (!file) goto done;
    s32 wrote = fwrite(edited, 1, strlen(edited), file) == strlen(edited);
    if (fclose(file) || !wrote) goto done;
    selected = assetCatalogGetMutable(hand_id);
    if (!selected) goto done;
    catalogSetOverride(selected, fileProviderHandle(edited_path));
    if (bgunBodyHandSourceIsCurrent(player, slot) ||
            !bgunQueueBodyHandModelLoad(player, slot, &player->gunctrl.gunmodeldef, NULL, NULL)) goto done;
    bgunTickGunLoad();
    if (!nativeTriangle(player->gunctrl.gunmodeldef, 13) ||
            !bgunBodyHandSourceIsCurrent(player, slot)) goto done;
    catalogReleaseStageAsset(ASSET_MODEL, hand_id);
    player->gunctrl.gunmodeldef = NULL;
    /* A request captured before source replacement must never consume the
     * replacement or fall back to the native-number sibling. */
    if (!bgunQueueBodyHandModelLoad(player, slot, &player->gunctrl.handmodeldef, NULL, NULL)) goto done;
    selected = assetCatalogGetMutable(hand_id);
    if (!selected) goto done;
    catalogSetOverride(selected, fileProviderHandle("$S/body-head-source-smoke/hand-alias/missing.obj"));
    bgunTickGunLoad();
    if (player->gunctrl.handmodeldef || player->gunctrl.gunloadstate != 5 ||
            bgunBodyHandSourceIsCurrent(player, slot)) goto done;
    if (!bgunQueueBodyHandModelLoad(player, slot, &player->gunctrl.gunmodeldef, NULL, NULL)) goto done;
    bgunTickGunLoad();
    if (player->gunctrl.gunmodeldef || player->gunctrl.gunloadstate != 5) goto done;
    ok = 1;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=hand_alias "
        "base_identity=1 selected_geometry=11 fists_geometry=13 shared_handle_identity=1 source_change_invalidates=1 "
        "stale_queue=rejected missing_source=rejected result=PASS");
done:
    if (player && (player->gunctrl.handmodeldef || player->gunctrl.gunmodeldef))
        catalogReleaseStageAsset(ASSET_MODEL, player->gunctrl.loadcatalogid);
    g_Vars.currentplayer = saved;
    free(player);
    if (assetCatalogResolveAny(body_id)) assetCatalogUnregister(body_id);
    if (assetCatalogResolveAny(mesh_id)) assetCatalogUnregister(mesh_id);
    if (assetCatalogResolveAny(hand_id)) assetCatalogUnregister(hand_id);
    if (assetCatalogResolveAny(other_id)) assetCatalogUnregister(other_id);
    if (slots) { assetCatalogRestoreCustomModelSlots(slots); assetCatalogDestroyCustomModelSlotSnapshot(slots); }
    return ok;
}

/* Reproduce boot's top-level first owner followed by the same mesh embedded
 * in a body. The actual binder repeats its comparison without prepared bytes. */
static s32 nestedFirstOwnerCase(void)
{
    const char *mesh_id = "smoke:nested_first_owner_mesh";
    const char *body_id = "smoke:nested_equal_body";
    const char *loose = "$S/body-head-source-smoke/shared/top/mesh.pdmesh";
    const char *equal = "$S/body-head-source-smoke/shared/equal/body.pdbody";
    const char *different = "$S/body-head-source-smoke/shared/different/body.pdbody";
    const char *nested = "$S/body-head-source-smoke/shared/equal/body.pdbody::edited_mesh.pdmesh";
    const char *divergent_nested = "$S/body-head-source-smoke/shared/different/body.pdbody::edited_mesh.pdmesh";
    char full[FS_MAXPATH + 1], original_path[FS_MAXPATH + 1];
    char disk_hash[SHA256_HEX_SIZE], bytes_hash[SHA256_HEX_SIZE], nested_hash[SHA256_HEX_SIZE];
    char divergent_hash[SHA256_HEX_SIZE], error[256];
    void *bytes = NULL;
    u32 size = 0;
    s32 ok = 0;
    asset_data_handle_t original;
    if (!fsCreateDir("$S/body-head-source-smoke/shared") ||
            !fsCreateDir("$S/body-head-source-smoke/shared/top") ||
            !fsCreateDir("$S/body-head-source-smoke/shared/equal") ||
            !fsCreateDir("$S/body-head-source-smoke/shared/different") ||
            !writeMesh(mesh_id, 7, "authored.obj", &bytes, &size) ||
            !writeBytes(loose, bytes, size) ||
            !writeBodyHead(equal, body_id, mesh_id, NULL, 0, 199, 1, 1, 0, 7, "authored.obj")) goto done;
    if (weaponGraphArchiveCanonicalSha256Bytes(bytes, size, bytes_hash) != 0 ||
            weaponGraphArchiveCanonicalSha256File(fsFullPath(loose, full, sizeof(full)), disk_hash) != 0 ||
            weaponGraphArchiveCanonicalSha256File(nested, nested_hash) != 0 ||
            strcmp(bytes_hash, disk_hash) || strcmp(disk_hash, nested_hash)) goto done;
    if (assetCatalogScanExternalLayoutFolder("smoke_shared_mesh",
            fsFullPath("$S/body-head-source-smoke/shared/top", full, sizeof(full))) != 1) goto done;
    const asset_entry_t *mesh = assetCatalogResolve(mesh_id);
    const char *path = mesh && mesh->source.primary.provider == fileProvider()
        ? fileProviderPath(mesh->source.primary) : NULL;
    if (!path || strlen(path) >= sizeof(original_path)) goto done;
    strcpy(original_path, path);
    original = mesh->source.primary;
    if (assetCatalogScanExternalLayoutFolder("smoke_shared_body",
            fsFullPath("$S/body-head-source-smoke/shared/equal", full, sizeof(full))) != 1 ||
            !checkInstalled(body_id, 0, mesh_id, NULL, 199, 1, 1)) goto done;
    mesh = assetCatalogResolve(mesh_id);
    if (!mesh || memcmp(&original, &mesh->source.primary, sizeof(original)) ||
            strcmp(fileProviderPath(mesh->source.primary), original_path)) goto done;
    const asset_entry_t *body = assetCatalogResolve(body_id);
    if (!body || body->ref_count != 0 || body->loaded_data || catalogGetLoadedModeldef(body_id) ||
            catalogGetLoadedModeldef(mesh_id) ||
            !loaderWalkerMeshSourceChangeAllowed(mesh->load_state, mesh->bundled,
                mesh->ref_count, mesh->loaded_data != NULL, mesh->payload_kind)) goto done;
    const s32 edges = catalogDepCount();
    if (!writeBodyHead(different, "smoke:nested_divergent_body", mesh_id, NULL,
            0, 199, 1, 1, 0, 13, "authored.obj") ||
            weaponGraphArchiveCanonicalSha256File(divergent_nested, divergent_hash) != 0 ||
            !strcmp(divergent_hash, nested_hash) ||
            loaderWalkerMeshSourceMatchesEntry(mesh, divergent_nested, NULL, 0, error, sizeof(error)) ||
            !strstr(error, "divergent typed mesh archives claim one catalog ID") ||
            assetCatalogScanExternalLayoutFolder("smoke_divergent_body",
                fsFullPath("$S/body-head-source-smoke/shared/different", full, sizeof(full))) >= 0 ||
            assetCatalogResolve("smoke:nested_divergent_body") || catalogDepCount() != edges) goto done;
    mesh = assetCatalogResolve(mesh_id);
    ok = mesh && !memcmp(&original, &mesh->source.primary, sizeof(original)) &&
        checkInstalled(body_id, 0, mesh_id, NULL, 199, 1, 0);
    if (ok) sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=nested_first_owner "
        "file_bytes_digest=%s selected=%s nonresident=1 hash_inequality=1 divergent=rejected result=PASS", nested_hash, original_path);
done:
    if (bytes) sysMemFree(bytes);
    return ok;
}

/* Insert into the existing body/head client harness; relies on its real
 * archive writer/nativeTriangle helpers. Draft only, not yet run. */
static s32 writeDirectMesh(const char *path, const char *id,
    const char *selection, const char *extra, s32 include_selected)
{
    char full[FS_MAXPATH + 1], ini[1024];
    const char *wrong = "v 0 0 0\nv 3 0 0\nv 0 2 0\nf 1 2 3\n";
    const char *selected = "v 0 0 0\nv 17 0 0\nv 0 2 0\nf 1 2 3\n";
    snprintf(ini, sizeof(ini), "[model]\nkind=mesh\ncatalog_id=%s\ngeometry_file=%s\n%s",
        id, selection, extra ? extra : "");
    mod_archive_writer_t *writer = modArchiveBegin(fsFullPath(path, full, sizeof(full)));
    if (!writer) return 0;
    if (!packText(writer, "mesh.ini", ini) || !packText(writer, "model.obj", wrong) ||
            (include_selected && !packText(writer, "custom.obj", selected)) ||
            !packText(writer, "_meta/manifest.json",
                "{\"pd_kind\":\"mesh\",\"pd_schema_version\":1,"
                "\"id\":\"stale:private\",\"geometry\":\"model.obj\","
                "\"source_filenum_symbol\":\"STALE_PRIVATE_SYMBOL\"}")) {
        modArchiveAbort(writer);
        return 0;
    }
    return modArchiveFinish(writer) == MODARCHIVE_OK;
}

/* Each route must consume the authored triangle or reject it. No resolver-only
 * success is counted as catalog/preview/queued-gun activation evidence. */
static s32 checkDirectMeshConsumers(const char *id, s32 body_slot,
    struct player *player, const char *path, s32 accepted)
{
    asset_entry_t *entry = assetCatalogGetMutable(id);
    if (!entry) return 0;
    asset_data_handle_t handle = fileProviderHandle(path);
    catalogSetOverride(entry, handle);
    s32 loaded = catalogLoadStageAsset(ASSET_MODEL, id);
    s32 matches = accepted ? loaded && nativeTriangle(catalogGetLoadedModeldef(id), 17) : !loaded;
    if (loaded) catalogReleaseStageAsset(ASSET_MODEL, id);
    if (!matches) return 0;
    struct modeldef *preview = modeldefLoadFromHandle(handle, FILE_GCOMBATHANDSLOD, NULL, 0, NULL);
    matches = accepted ? nativeTriangle(preview, 17) : !preview;
    if (preview) catalogReleaseStageAsset(ASSET_MODEL, id);
    if (!matches || !bgunQueueBodyHandModelLoad(player, body_slot,
            &player->gunctrl.handmodeldef, NULL, NULL)) return 0;
    bgunTickGunLoad();
    matches = accepted ? nativeTriangle(player->gunctrl.handmodeldef, 17) :
        !player->gunctrl.handmodeldef && player->gunctrl.gunloadstate == 5;
    if (player->gunctrl.handmodeldef) catalogReleaseStageAsset(ASSET_MODEL, id);
    player->gunctrl.handmodeldef = NULL;
    return matches;
}

static s32 directTypedModelSourceCase(void)
{
    const char *root = "$S/body-head-source-smoke/direct-model";
    const char *body_path = "$S/body-head-source-smoke/direct-model/body.pdbody";
    const char *body_id = "smoke:direct_model_body";
    const char *mesh_id = "smoke:direct_model_body_mesh";
    const char *hand_id = "smoke:direct_model_hand";
    const char *good = "$S/body-head-source-smoke/direct-model/good.pdmesh";
    struct player *saved = g_Vars.currentplayer;
    struct player *player = calloc(1, sizeof(*player));
    void *slots = assetCatalogSnapshotCustomModelSlots();
    void *nested_bytes = NULL;
    u32 nested_size = 0;
    char error[256], full[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
    s32 ok = 0;
    if (!player || !slots || !fsCreateDir(root) ||
            !writeBodyHead(body_path, body_id, mesh_id, hand_id, 0, 199, 1, 0, 0, 7, "authored.obj")) goto done;
    asset_entry_t *hand = assetCatalogRegister(hand_id, ASSET_MODEL);
    if (!hand) goto done;
    hand->source_filenum = FILE_GCOMBATHANDSLOD;
    hand->enabled = 1;
    if (!assetCatalogRegisterBodyHeadArchive(body_path, body_id, 0, -1, 0, error, sizeof(error))) goto done;
    const asset_entry_t *body = assetCatalogResolve(body_id);
    if (!body) goto done;
    s32 slot = body->runtime_index;
    g_Vars.currentplayer = player;
    struct { const char *name, *selection, *extra; s32 include_selected, accepted; } cases[] = {
        {"good", "custom.obj", "", 1, 1},
        {"missing", "missing.obj", "", 1, 0},
        {"conflict", "custom.obj", "geometry=model.obj\n", 1, 0},
        {"traversal", "../custom.obj", "", 1, 0},
        {"unsupported", "geometry.bin", "", 1, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s.pdmesh", root, cases[i].name);
        if (!writeDirectMesh(path, hand_id, cases[i].selection, cases[i].extra, cases[i].include_selected) ||
                !checkDirectMeshConsumers(hand_id, slot, player, path, cases[i].accepted)) goto done;
        sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: direct_model=%s catalog=1 preview=1 queue=1 result=PASS", cases[i].name);
    }
    nested_bytes = fsFileLoad(good, &nested_size);
    if (!nested_bytes || !nested_size) goto done;
    snprintf(path, sizeof(path), "%s/outer.pdmesh", root);
    mod_archive_writer_t *writer = modArchiveBegin(fsFullPath(path, full, sizeof(full)));
    if (!writer) goto done;
    if (!packText(writer, "mesh.ini", "[model]\nkind=mesh\ncatalog_id=smoke:outer\ngeometry_file=model.obj\n") ||
            !packText(writer, "model.obj", "v 0 0 0\nv 3 0 0\nv 0 2 0\nf 1 2 3\n") ||
            modArchiveAddFileMem(writer, "inner.pdmesh", nested_bytes, nested_size) != MODARCHIVE_OK) {
        modArchiveAbort(writer);
        goto done;
    }
    if (modArchiveFinish(writer) != MODARCHIVE_OK) goto done;
    snprintf(path, sizeof(path), "%s/outer.pdmesh::inner.pdmesh", root);
    if (!checkDirectMeshConsumers(hand_id, slot, player, path, 1)) goto done;
    snprintf(path, sizeof(path), "%s/raw.bin", root);
    FILE *file = fsFileOpenWrite(path);
    if (!file) goto done;
    s32 wrote = fwrite("unsupported model bytes", 1, 23, file) == 23;
    if (fclose(file) || !wrote || !checkDirectMeshConsumers(hand_id, slot, player, path, 0)) goto done;
    ok = 1;
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: witness=direct_model "
        "custom_geometry=17 wrong_sibling=3 stale_private=ignored nested=1 missing=rejected "
        "conflicts=rejected unsafe=rejected binary=rejected catalog_preview_queue=1 result=PASS");
done:
    g_Vars.currentplayer = saved;
    free(player);
    if (nested_bytes) sysMemFree(nested_bytes);
    if (assetCatalogResolveAny(body_id)) assetCatalogUnregister(body_id);
    if (assetCatalogResolveAny(mesh_id)) assetCatalogUnregister(mesh_id);
    if (assetCatalogResolveAny(hand_id)) assetCatalogUnregister(hand_id);
    if (slots) { assetCatalogRestoreCustomModelSlots(slots); assetCatalogDestroyCustomModelSlotSnapshot(slots); }
    return ok;
}

int bodyHeadSourceHarnessRun(void)
{
    if (!smokeHarnessIsActive() || !fsCreateDir(k_root)) return -1;
    struct { const char *name; s32 (*run)(void); } cases[] = {
        {"base_head_native_and_mp_identity", baseHeadIdentityCase},
        {"established_native_alias_uses_selected_catalog_source", nativeMeshAliasCase},
        {"hand_alias_and_fists_use_selected_catalog_source", handSourceIdentityCase},
        {"direct_typed_model_source_in_all_consumers", directTypedModelSourceCase},
        {"nested_hash_and_shared_first_owner_binding", nestedFirstOwnerCase},
        {"scanner_public_edit_and_native_geometry", scannerCase},
        {"resident_same_path_edit_requires_release_and_recompile", residentGeometryCase},
        {"walker_public_identity_and_metadata_absent", walkerCase},
        {"post_child_source_collision_rollback", rollbackCase},
        {"received_public_edit_and_resident_rollback", receivedCase}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        s32 ok = cases[i].run();
        sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: case=%s result=%s", cases[i].name, ok ? "PASS" : "FAIL");
        if (!ok) return -1;
    }
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.BODYHEAD: cases=10 result=PASS");
    return 0;
}
