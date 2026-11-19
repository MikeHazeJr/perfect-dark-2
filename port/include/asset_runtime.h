#ifndef _IN_ASSET_RUNTIME_H
#define _IN_ASSET_RUNTIME_H

#include "assetcatalog.h"
#include "character_head_policy.h"
#include "vehicle_graph_runner.h"

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
    char character_body_id[CATALOG_ID_LEN];
    char character_head_id[CATALOG_ID_LEN];
    character_head_policy_e character_head_policy;
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
    /*
     * Parsed public-source state. Descriptor fields above locate authored
     * members; these fields are populated only by
     * assetRuntimeHydrateCatalogEntry after the selected .pdxxx member has
     * been read and validated. Production consumers must not infer these
     * values from legacy tables.
     */
    s32 source_hydrated;
    s32 source_enabled;
    f32 source_opacity;
    char material_shading_model[32];
    f32 material_base_color[4];
    f32 material_roughness;
    f32 material_metallic;
    s32 material_emissive;
    char skin_material_id[CATALOG_ID_LEN];
    f32 skin_swatch_color[4];
    u32 prop_flags;
    f32 prop_health;
    f32 vehicle_turn_input_scale;
    f32 vehicle_reverse_turn_gain;
    f32 vehicle_steering_response_ntsc;
    f32 vehicle_steering_response_pal;
    f32 vehicle_turn_visual_scale;
    f32 vehicle_input_response;
    f32 vehicle_forward_input_scale;
    f32 vehicle_lateral_input_scale;
    f32 vehicle_lean_response;
    f32 vehicle_forward_base;
    f32 vehicle_forward_accel_gain;
    f32 vehicle_reverse_base;
    f32 vehicle_drag_ntsc;
    f32 vehicle_drag_pal;
    f32 vehicle_forward_thrust;
    f32 vehicle_lateral_thrust;
    f32 vehicle_forward_tilt;
    f32 vehicle_lateral_tilt;
    f32 vehicle_tilt_response_ntsc;
    f32 vehicle_tilt_response_pal;
    f32 vehicle_yaw_response_ntsc;
    f32 vehicle_yaw_response_pal;
    /* The original full-throttle bonus, from physics v2 only (a v1 graph's
     * og.vehicle.throttle_boost holds its own values). */
    f32 vehicle_boost_speed;
    s32 vehicle_boost_time_ticks60;
    /* v2 behavior: the file's booleans. v1 graph: derived from it (mount =
     * the mount event reaches og.vehicle.mount_seat, drive = the tick reaches
     * og.vehicle.hover_drive, dismount = the dismount event reaches
     * og.vehicle.dismount_seat), so every caller of assetRuntimeVehicleAllows
     * keeps one answer for both. */
    s32 vehicle_allow_mount;
    s32 vehicle_allow_drive;
    s32 vehicle_allow_dismount;
    f32 vehicle_hover[13];
    /* pd.vehicle_graph.v1 (context/designs/modding/vehicle-graph-v1.md): the
     * compiled behavior graph with its behavior/variables.json when the
     * vehicle's behavior.graph.json is v1, owned by this binding and freed by
     * assetRuntimeReset; NULL for a v2 behavior file. */
    vehicle_graph_program_t *vehicle_graph;
} asset_runtime_binding_t;

void assetRuntimeReset(void);
s32 assetRuntimeSupportsType(asset_type_e type);
s32 assetRuntimeActivateCatalogEntry(const asset_entry_t *entry,
                                     const char *primary_path);
s32 assetRuntimeHydrateCatalogEntry(const asset_entry_t *entry);
void assetRuntimeReleaseCatalogEntry(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFind(const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeAndId(asset_type_e type,
                                                          const char *asset_id);
const asset_runtime_binding_t *assetRuntimeFindByTypeKind(asset_type_e type,
                                                         s32 kind);
const asset_runtime_binding_t *assetRuntimeFindByTarget(asset_type_e type,
                                                       const char *target_id);
const asset_runtime_binding_t *assetRuntimeFindByRuntimeId(asset_type_e type,
                                                          s32 runtime_id);
const asset_runtime_binding_t *assetRuntimeHudElement(s32 element_type);
const asset_runtime_binding_t *assetRuntimeVehicleForModelnum(s32 modelnum);
/* Borrow the privately bound, typed embedded model of one enabled custom
 * vehicle definition. Available before its runtime graph is hydrated. */
const asset_entry_t *assetRuntimeVehiclePlacementModel(const char *vehicle_id);
/* Check the exact detached source through the native vehicle hydrator. */
s32 assetRuntimeVehiclePlacementSourceValid(const asset_entry_t *vehicle,
	char *err, size_t err_cap);
s32 assetRuntimeHudElementEnabled(s32 element_type);
s32 assetRuntimeVehicleAllows(s32 modelnum, const char *action);
s32 assetRuntimeVehicleHover(s32 modelnum, f32 out_values[13]);
/* Why the last vehicle hydration refused its sources ("" when it did not). */
const char *assetRuntimeVehicleGraphError(void);
/*
 * Catalog registration's check of a vehicle's behavior graph: a
 * pd.vehicle_graph.v1 graph is compiled with its variables and its parts are
 * checked against the model's part tables, so a vehicle with a broken graph is
 * refused when it registers, not when it is first used. model_path is the
 * vehicle's model as the catalog holds it and behavior_member its behavior
 * file. Returns 1 when the vehicle has no v1 graph or its graph compiles, 0
 * with the reason in err.
 */
s32 assetRuntimeVehicleGraphAdmits(const char *model_path, const char *behavior_member,
                                   const char *catalog_id, char *err, size_t err_cap);
s32 assetRuntimeSkinAppearance(const char *target_id, f32 out_rgba[4],
                               f32 *out_roughness, f32 *out_metallic,
                               s32 *out_emissive);
s32 assetRuntimePrimaryFileAccessible(const asset_runtime_binding_t *binding);
void *assetRuntimeLoadPrimaryFile(const asset_runtime_binding_t *binding,
                                  u32 *out_size);
s32 assetRuntimeCount(asset_type_e type);
s32 assetRuntimeAccessibleCount(asset_type_e type);

#ifdef __cplusplus
}
#endif

#endif
