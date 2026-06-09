#ifndef _IN_ASSET_RUNTIME_H
#define _IN_ASSET_RUNTIME_H

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_RUNTIME_INITIAL_BINDINGS 256

typedef struct asset_runtime_binding {
    s32 active;
    asset_type_e type;
    char id[CATALOG_ID_LEN];
    char primary_path[FS_MAXPATH];
    char authored_file[FS_MAXPATH];
    char dependency_a[FS_MAXPATH];
    char dependency_b[FS_MAXPATH];
    char dependency_c[FS_MAXPATH];
    char dependency_d[FS_MAXPATH];
    char dependency_e[FS_MAXPATH];
    char scenario_rooms_file[FS_MAXPATH];
    char scenario_portals_file[FS_MAXPATH];
    char scenario_pads_file[FS_MAXPATH];
    char scenario_spawns_file[FS_MAXPATH];
    char scenario_volumes_file[FS_MAXPATH];
    char scenario_objects_file[FS_MAXPATH];
    char scenario_setup_fields_file[FS_MAXPATH];
    char scenario_ai_lists_file[FS_MAXPATH];
    char scenario_objectives_file[FS_MAXPATH];
    char scenario_navigation_file[FS_MAXPATH];
    char scenario_navigation_waypoints_file[FS_MAXPATH];
    char scenario_navigation_waygroups_file[FS_MAXPATH];
    char scenario_navigation_covers_file[FS_MAXPATH];
    char scenario_navigation_paths_file[FS_MAXPATH];
    char lang_locale[16];
    char lang_category[32];
    u32 lang_string_count;
    s32 ui_width;
    s32 ui_height;
    s32 ui_data_size;
    s32 ui_nineslice_left;
    s32 ui_nineslice_right;
    s32 ui_nineslice_top;
    s32 ui_nineslice_bottom;
    char ui_nineslice_edge_mode[16];
    char ui_nineslice_center_mode[16];
    char target_id[CATALOG_ID_LEN];
    char shader_id[64];
    char gamemode_name[64];
    char gamemode_description[256];
    char weapon_model_file[FS_MAXPATH];
    s32 gamemode_min_players;
    s32 gamemode_max_players;
    s32 gamemode_team_based;
    s32 gamemode_requirefeature;
    s32 bot_profile_type;
    s32 bot_profile_difficulty;
    s32 bot_profile_body;
    s32 bot_profile_name_langid;
    s32 bot_profile_requirefeature;
    char display_name[64];
    s32 name_langid;
    s32 requirefeature;
    s32 runtime_id;
    s32 kind;
    s32 target_kind;
    f32 value0;
    f32 params[4];
} asset_runtime_binding_t;

void assetRuntimeReset(void);
s32 assetRuntimeSupportsType(asset_type_e type);
s32 assetRuntimeActivateCatalogEntry(const asset_entry_t *entry,
                                     const char *primary_path);
void assetRuntimeReleaseCatalogEntry(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFind(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeAndId(asset_type_e type,
                                                          const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeKind(asset_type_e type,
                                                         s32 kind);
const asset_runtime_binding_t *assetRuntimeFindByTarget(asset_type_e type,
                                                       const char *target_id);
s32 assetRuntimePrimaryFileAccessible(const asset_runtime_binding_t *binding);
void *assetRuntimeLoadPrimaryFile(const asset_runtime_binding_t *binding,
                                  u32 *out_size);
s32 assetRuntimeCount(asset_type_e type);
s32 assetRuntimeAccessibleCount(asset_type_e type);

#ifdef __cplusplus
}
#endif

#endif
