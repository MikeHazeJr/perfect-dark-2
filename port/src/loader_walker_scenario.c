/**
 * loader_walker_scenario.c -- Step 4 (2026-05-03).
 *
 * Walks scenario .pdscenario files and registers each as ASSET_SCENARIO.
 * The public scene.glb/scene.gltf member is the DCC-openable runtime source;
 * collision/navigation/setup tables remain authored archive members.
 * setup.fields.json is the decoded setup-field source, not a raw setup dump.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "modvfs.h"
#include "stage_archive_repair.h"
#include "system.h"

static s32 s_kindToMode(const char *kind)
{
    if (!kind || !kind[0]) {
        return 0;
    }
    if (strcmp(kind, "mp") == 0 || strcmp(kind, "multiplayer") == 0) {
        return MAP_MODE_MP;
    }
    if (strcmp(kind, "solo") == 0 || strcmp(kind, "campaign") == 0) {
        return MAP_MODE_SOLO;
    }
    if (strcmp(kind, "firingrange") == 0) {
        return MAP_MODE_SOLO;
    }
    if (strcmp(kind, "coop") == 0 || strcmp(kind, "co-op") == 0) {
        return MAP_MODE_COOP;
    }
    return 0;
}

static void s_copyMemberPath(char *out, size_t out_n,
                             const char *archive_path, const char *member)
{
    char member_path[FS_MAXPATH + 1];

    if (!out || out_n == 0) {
        return;
    }
    out[0] = '\0';

    if (loaderWalkerArchiveMemberPath(archive_path, member,
                                      member_path, sizeof(member_path))) {
        strncpy(out, member_path, out_n - 1);
        out[out_n - 1] = '\0';
    }
}

static void s_copyManifestMember(char *out, size_t out_n,
                                 const char *manifest, size_t manifest_len,
                                 const char *archive_path,
                                 const char *manifest_key,
                                 const char *default_member)
{
    char member[FS_MAXPATH];

    if (!out || out_n == 0) {
        return;
    }
    out[0] = '\0';

    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, manifest_key,
                                     member, sizeof(member))) {
        strncpy(member, default_member, sizeof(member) - 1);
        member[sizeof(member) - 1] = '\0';
    }

    s_copyMemberPath(out, out_n, archive_path, member);
}

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    (void)pd_kind;

    s64 stagenum = 0;
    char kind[32];
    char scene_member[FS_MAXPATH];
    char collision_member[FS_MAXPATH];
    char graph_member[FS_MAXPATH];
    char member_path[FS_MAXPATH + 1];
    s64 room_count = 0;

    if (!loaderWalkerEnvelopeInt(manifest, manifest_len, "room_count",
                                 &room_count)
            || room_count < 2 || room_count > 32768) {
        sysLoudFailf("LOAD.UNIVERSAL.SCENARIO",
            "missing or invalid room_count in typed archive id=%s path=%s",
            id ? id : "(null)", file_path ? file_path : "(null)");
        return -1;
    }

    loaderWalkerEnvelopeInt(manifest, manifest_len, "stagenum", &stagenum);
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "kind",
                                 kind, sizeof(kind));
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "runtime_source",
                                     scene_member, sizeof(scene_member))) {
        if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "scene",
                                         scene_member, sizeof(scene_member))) {
            strncpy(scene_member, "scene.glb", sizeof(scene_member) - 1);
            scene_member[sizeof(scene_member) - 1] = '\0';
        }
    }
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "collision_source",
                                     collision_member, sizeof(collision_member))) {
        collision_member[0] = '\0';
    }
    if (!loaderWalkerEnvelopePathCopy(manifest, manifest_len, "level_graph",
                                     graph_member, sizeof(graph_member))) {
        strncpy(graph_member, "level.graph.json", sizeof(graph_member) - 1);
        graph_member[sizeof(graph_member) - 1] = '\0';
    }

    asset_entry_t *e = assetCatalogRegister(id, ASSET_SCENARIO);
    if (!e) return -1;
    loaderWalkerMarkBaseArchiveEntry(e);
    e->ext.scenario.stagenum = (s32)stagenum;
    e->ext.scenario.mode = s_kindToMode(kind);
    e->ext.scenario.source_room_count = (s32)room_count;
    /* The stage's language bank (stage text banks off lang.c, 2026-11-21):
     * an ASSET_LANG catalog id the stage names itself. langGetStageBank
     * resolves it; an archive that names none keeps the game's stage table. */
    e->ext.scenario.text_bank[0] = '\0';
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "text_bank",
                                 e->ext.scenario.text_bank,
                                 sizeof(e->ext.scenario.text_bank));

    if (loaderWalkerArchiveMemberPath(file_path, scene_member,
                                      member_path, sizeof(member_path))) {
        strncpy(e->ext.scenario.scene_file, member_path,
                sizeof(e->ext.scenario.scene_file) - 1);
        e->ext.scenario.scene_file[sizeof(e->ext.scenario.scene_file) - 1] = '\0';
        catalogSetPrimaryFile(e, member_path);
    }
    if (collision_member[0]
            && loaderWalkerArchiveMemberPath(file_path, collision_member,
                                             member_path, sizeof(member_path))) {
        strncpy(e->ext.scenario.collision_file, member_path,
                sizeof(e->ext.scenario.collision_file) - 1);
        e->ext.scenario.collision_file[sizeof(e->ext.scenario.collision_file) - 1] = '\0';
    }
    if (loaderWalkerArchiveMemberPath(file_path, graph_member,
                                      member_path, sizeof(member_path))) {
        strncpy(e->ext.scenario.level_graph_file, member_path,
                sizeof(e->ext.scenario.level_graph_file) - 1);
        e->ext.scenario.level_graph_file[sizeof(e->ext.scenario.level_graph_file) - 1] = '\0';
    }

    s_copyManifestMember(e->ext.scenario.portals_file,
                         sizeof(e->ext.scenario.portals_file),
                         manifest, manifest_len, file_path,
                         "portals", "portals.json");
    s_copyManifestMember(e->ext.scenario.pads_file,
                         sizeof(e->ext.scenario.pads_file),
                         manifest, manifest_len, file_path,
                         "pads", "pads.json");
    s_copyManifestMember(e->ext.scenario.spawns_file,
                         sizeof(e->ext.scenario.spawns_file),
                         manifest, manifest_len, file_path,
                         "spawns", "spawns.json");
    s_copyManifestMember(e->ext.scenario.volumes_file,
                         sizeof(e->ext.scenario.volumes_file),
                         manifest, manifest_len, file_path,
                         "volumes", "volumes.json");
    s_copyManifestMember(e->ext.scenario.objects_file,
                         sizeof(e->ext.scenario.objects_file),
                         manifest, manifest_len, file_path,
                         "objects", "objects.json");
    s_copyManifestMember(e->ext.scenario.setup_fields_file,
                         sizeof(e->ext.scenario.setup_fields_file),
                         manifest, manifest_len, file_path,
                         "setup_fields", "setup.fields.json");
    s_copyManifestMember(e->ext.scenario.ai_lists_file,
                         sizeof(e->ext.scenario.ai_lists_file),
                         manifest, manifest_len, file_path,
                         "ai_lists", "ai/ailists.json");
    s_copyManifestMember(e->ext.scenario.objectives_file,
                         sizeof(e->ext.scenario.objectives_file),
                         manifest, manifest_len, file_path,
                         "objectives", "objectives.json");
    s_copyManifestMember(e->ext.scenario.navigation_file,
                         sizeof(e->ext.scenario.navigation_file),
                         manifest, manifest_len, file_path,
                         "navigation", "navigation.ini");
    s_copyManifestMember(e->ext.scenario.navigation_waypoints_file,
                         sizeof(e->ext.scenario.navigation_waypoints_file),
                         manifest, manifest_len, file_path,
                         "waypoints", "navigation/waypoints.json");
    s_copyManifestMember(e->ext.scenario.navigation_waygroups_file,
                         sizeof(e->ext.scenario.navigation_waygroups_file),
                         manifest, manifest_len, file_path,
                         "waygroups", "navigation/waygroups.json");
    s_copyManifestMember(e->ext.scenario.navigation_covers_file,
                         sizeof(e->ext.scenario.navigation_covers_file),
                         manifest, manifest_len, file_path,
                         "covers", "navigation/covers.json");
    s_copyManifestMember(e->ext.scenario.navigation_paths_file,
                         sizeof(e->ext.scenario.navigation_paths_file),
                         manifest, manifest_len, file_path,
                         "paths", "navigation/paths.json");
    return 1;
}

s32 loaderWalkerPublishScenarioArchive(const char *file_path, const char *expected_id)
{
    static const char prefix[] = "base:scenario_";
    if (!file_path || !expected_id || strncmp(expected_id, prefix, sizeof(prefix) - 1) != 0
            || assetCatalogResolveAny(expected_id)) return 0;

    const char *slug = expected_id + sizeof(prefix) - 1;
    if (!slug[0] || slug[strspn(slug, "abcdefghijklmnopqrstuvwxyz0123456789_-")]) return 0;

    char stage_id[CATALOG_ID_LEN];
    const int stage_id_length = snprintf(stage_id, sizeof(stage_id), "base:%s", slug);
    if (stage_id_length < 0 || (size_t)stage_id_length >= sizeof(stage_id)) return 0;
    const asset_entry_t *stage = assetCatalogResolveAny(stage_id);
    if (!stage || stage->type != ASSET_MAP || !stage->bundled) return 0;
    const s32 expected_stagenum = stage->ext.map.stagenum;

    char data_root[FS_MAXPATH + 1];
    char canonical_path[FS_MAXPATH + 1];
    fsDataDir(data_root, sizeof(data_root));
    int length = snprintf(canonical_path, sizeof(canonical_path),
        "%s/scenarios/base_scenario_%s.pdscenario", data_root, slug);
    if (length < 0 || (size_t)length >= sizeof(canonical_path)
            || strcmp(canonical_path, file_path) != 0 || modVfsCanResolve(file_path)) return 0;

    /* A loose override can shadow the canonical file even without a VFS mount. */
    char base_path[FS_MAXPATH + 1];
    char resolved_path[FS_MAXPATH + 1];
    length = snprintf(base_path, sizeof(base_path), "%s/%s", fsGetBaseDir(), file_path);
    if (length < 0 || (size_t)length >= sizeof(base_path)
            || strcmp(fsFullPath(file_path, resolved_path, sizeof(resolved_path)), base_path) != 0) return 0;

    char *manifest = NULL;
    size_t manifest_len = 0;
    if (!loaderWalkerArchiveTextMember(base_path, ASSET_ARCHIVE_META_MANIFEST_PATH,
            &manifest, &manifest_len)) return 0;

    char kind[32], id[CATALOG_ID_LEN];
    s64 stagenum = 0;
    const s32 published = loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "pd_kind", kind, sizeof(kind))
        && strcmp(kind, "scenario") == 0
        && loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "id", id, sizeof(id))
        && strcmp(id, expected_id) == 0
        && loaderWalkerEnvelopeInt(manifest, manifest_len, "stagenum", &stagenum)
        && stagenum == expected_stagenum
        && s_register(manifest, manifest_len, kind, id, file_path) == 1;
    sysMemFree(manifest);
    if (!published) return 0;

    const asset_entry_t *entry = assetCatalogResolveAny(expected_id);
    if (entry && !assetHandleIsNull(entry->source.primary)) return 1;
    assetCatalogUnregister(expected_id);
    return 0;
}

void loaderWalkerScanScenarios(const char *tier_dir,
                                loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "scenario",
        .subdir = "scenarios",
        .extension = ".pdscenario",
        .always_invoke = 1,
        .skip_path = stageArchiveRepairIsPendingPath,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
