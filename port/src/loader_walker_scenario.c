/**
 * loader_walker_scenario.c -- Step 4 (2026-05-03).
 *
 * Walks scenario .pdscenario files and registers each as ASSET_SCENARIO.
 * The public scene.glb/scene.gltf member is the DCC-openable runtime source;
 * collision/navigation/setup tables remain authored archive members.
 * setup.fields.tsv is the decoded setup-field source, not a raw setup dump.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"

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
    char member[128];

    if (!out || out_n == 0) {
        return;
    }
    out[0] = '\0';

    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, manifest_key,
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
    char scene_member[128];
    char collision_member[128];
    char graph_member[128];
    char member_path[FS_MAXPATH + 1];
    loaderWalkerEnvelopeInt(manifest, manifest_len, "stagenum", &stagenum);
    loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "kind",
                                 kind, sizeof(kind));
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "runtime_source",
                                     scene_member, sizeof(scene_member))) {
        if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "scene",
                                         scene_member, sizeof(scene_member))) {
            strncpy(scene_member, "scene.glb", sizeof(scene_member) - 1);
            scene_member[sizeof(scene_member) - 1] = '\0';
        }
    }
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "collision_source",
                                     collision_member, sizeof(collision_member))) {
        collision_member[0] = '\0';
    }
    if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "level_graph",
                                     graph_member, sizeof(graph_member))) {
        strncpy(graph_member, "level.graph.json", sizeof(graph_member) - 1);
        graph_member[sizeof(graph_member) - 1] = '\0';
    }

    asset_entry_t *e = assetCatalogRegister(id, ASSET_SCENARIO);
    if (!e) return -1;
    loaderWalkerMarkBaseArchiveEntry(e);
    e->ext.scenario.stagenum = (s32)stagenum;
    e->ext.scenario.mode = s_kindToMode(kind);

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

    s_copyManifestMember(e->ext.scenario.pads_file,
                         sizeof(e->ext.scenario.pads_file),
                         manifest, manifest_len, file_path,
                         "pads", "pads.tsv");
    s_copyManifestMember(e->ext.scenario.spawns_file,
                         sizeof(e->ext.scenario.spawns_file),
                         manifest, manifest_len, file_path,
                         "spawns", "spawns.tsv");
    s_copyManifestMember(e->ext.scenario.volumes_file,
                         sizeof(e->ext.scenario.volumes_file),
                         manifest, manifest_len, file_path,
                         "volumes", "volumes.tsv");
    s_copyManifestMember(e->ext.scenario.objects_file,
                         sizeof(e->ext.scenario.objects_file),
                         manifest, manifest_len, file_path,
                         "objects", "objects.tsv");
    s_copyManifestMember(e->ext.scenario.setup_fields_file,
                         sizeof(e->ext.scenario.setup_fields_file),
                         manifest, manifest_len, file_path,
                         "setup_fields", "setup.fields.tsv");
    s_copyManifestMember(e->ext.scenario.objectives_file,
                         sizeof(e->ext.scenario.objectives_file),
                         manifest, manifest_len, file_path,
                         "objectives", "objectives.tsv");
    s_copyManifestMember(e->ext.scenario.navigation_file,
                         sizeof(e->ext.scenario.navigation_file),
                         manifest, manifest_len, file_path,
                         "navigation", "navigation.ini");
    return 1;
}

void loaderWalkerScanScenarios(const char *tier_dir,
                                loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        "scenario", "scenarios", ".pdscenario",
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
