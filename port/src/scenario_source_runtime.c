#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/gu.h>
#include <PR/ultratypes.h>

#include "constants.h"
#include "types.h"
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "asset_source_debug.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "bss.h"
#include "game/bondmove.h"
#include "game/bg.h"
#include "game/atan2f.h"
#include "game/cheats.h"
#include "game/bondgun.h"
#include "game/chraction.h"
#include "game/chr.h"
#include "game/chrai.h"
#include "game/debug.h"
#include "game/dlights.h"
#include "game/env.h"
#include "game/explosions.h"
#include "game/hudmsg.h"
#include "game/inv.h"
#include "game/lang.h"
#include "game/lv.h"
#include "game/mpstats.h"
#include "game/gamefile.h"
#include "game/game_0b0fd0.h"
#include "game/modelmgr.h"
#include "game/music.h"
#include "game/objectives.h"
#include "game/options.h"
#include "game/pad.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/playerreset.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/propsnd.h"
#include "game/setuputils.h"
#include "game/smoke.h"
#include "game/stagetable.h"
#include "game/title.h"
#include "game/training.h"
#include "game/weather.h"
#include "lib/ailist.h"
#include "lib/anim.h"
#include "lib/lib_317f0.h"
#include "lib/main.h"
#include "lib/model.h"
#include "lib/music.h"
#include "lib/mtx.h"
#include "lib/meshcollision.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "../../src/lib/naudio/n_sndp.h"
#include "lib/snd.h"
#include "scenario_source_runtime.h"
#include "bss.h"
#include "data.h"

#define AI_QUIP_AUDIO_NEEDS_MOVEMENT(soundnum) \
		((soundnum) == SFX_M2_STOP_MOVING \
		|| (soundnum) == SFX_M1_STOP_DODGING \
		|| (soundnum) == SFX_F_STAND_STILL)

extern s16 g_GuardQuipBank[][4];
extern s16 g_SpecialQuipBank[][4];
extern s16 g_SkedarQuipBank[][4];
extern s16 g_MaianQuipBank[][4];
extern s16 g_QuipTexts[][4];
extern s16 g_CiMainQuips[][3];
extern s16 g_CiGreetingQuips[][3];
extern s16 g_CiAnnoyedQuips[][3];
extern s16 g_CiThanksQuips[];

typedef struct scenario_source_pad_row {
	s32 room;
	s32 liftnum;
	u32 flags;
	f32 pos[3];
	f32 up[3];
	f32 look[3];
	f32 bbox[6];
} scenario_source_pad_row_t;

#define SCENARIO_SOURCE_PORTAL_MAX_VERTICES 32

typedef struct scenario_source_portal_row {
	s32 room1;
	s32 room2;
	u32 flags;
	s32 vertex_count;
	struct coord vertices[SCENARIO_SOURCE_PORTAL_MAX_VERTICES];
} scenario_source_portal_row_t;

typedef struct scenario_source_segment_list {
	u32 *values;
	s32 count;
} scenario_source_segment_list_t;

typedef struct scenario_source_waypoint_row {
	s32 padnum;
	s32 groupnum;
	s32 step;
	scenario_source_segment_list_t neighbours;
} scenario_source_waypoint_row_t;

typedef struct scenario_source_waygroup_row {
	s32 step;
	scenario_source_segment_list_t waypoints;
	scenario_source_segment_list_t neighbours;
} scenario_source_waygroup_row_t;

typedef struct scenario_source_cover_row {
	f32 pos[3];
	f32 look[3];
	u32 flags;
} scenario_source_cover_row_t;

typedef struct scenario_source_path_row {
	s32 id;
	u32 flags;
	s32 *pads;
	s32 pad_count;
} scenario_source_path_row_t;

typedef struct scenario_source_path_table {
	scenario_source_path_row_t *rows;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_path_table_t;

typedef struct scenario_source_volume_row {
	char id[32];
	char kind[32];
	char shape[16];
	s32 padnum;
	s32 room;
	f32 min[3];
	f32 max[3];
} scenario_source_volume_row_t;

typedef struct scenario_source_navigation {
	scenario_source_waypoint_row_t *waypoints;
	s32 waypoint_count;
	scenario_source_waygroup_row_t *waygroups;
	s32 waygroup_count;
	scenario_source_cover_row_t *covers;
	s32 cover_count;
} scenario_source_navigation_t;

typedef struct scenario_source_setup_record {
	char id[32];
	char kind[64];
	u8 type;
	s32 order;
	u8 *bytes;
	u32 len;
} scenario_source_setup_record_t;

typedef struct scenario_source_setup_table {
	scenario_source_setup_record_t *records;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_setup_table_t;

typedef struct scenario_source_ai_command {
	u16 opcode;
	u8 *operands;
	u32 operand_count;
} scenario_source_ai_command_t;

typedef struct scenario_source_ai_list {
	char ref[32];
	s32 id;
	scenario_source_ai_command_t *commands;
	s32 count;
	s32 capacity;
	u32 byte_count;
} scenario_source_ai_list_t;

typedef struct scenario_source_ai_table {
	scenario_source_ai_list_t *lists;
	s32 count;
	s32 capacity;
	u32 byte_count;
	char error[192];
} scenario_source_ai_table_t;

typedef struct scenario_source_spawn_row {
	s32 padnum;
	s32 team;
} scenario_source_spawn_row_t;

typedef struct scenario_source_spawn_table {
	scenario_source_spawn_row_t *rows;
	s32 count;
	s32 capacity;
	char error[192];
} scenario_source_spawn_table_t;

typedef struct scenario_source_objective_node {
	char objective_id[32];
	char graph_node[96];
	char text_token[64];
	char difficulty_mask[32];
	u32 difficulty_bits;
	s32 criteria_start;
	s32 criteria_count;
	s32 inserted;
} scenario_source_objective_node_t;

typedef struct scenario_source_objective_criteria {
	char objective_id[32];
	char kind[64];
	char graph_node[96];
	char operand_kind[48];
	char target_ref[32];
	char target_record_ref[32];
	char pad_ref[32];
	char state_ref[64];
	u8 type;
	u32 stage_flag_mask;
	s32 tag_id;
	s32 pad;
	s32 match_value;
	s32 initial_status;
	s32 runtime_status;
	s32 runtime_status_valid;
	s32 runtime_object_state_valid;
	s32 runtime_object_present;
	s32 runtime_object_healthy;
	s32 runtime_object_held_by_player;
	s32 objective_index;
	s32 matched;
	const void *runtime_criteria;
} scenario_source_objective_criteria_t;

typedef struct scenario_source_setup_link {
	char record_id[32];
	char kind[64];
	u8 type;
	s32 order;
	s32 target[3];
	s32 aux[2];
	s32 matched;
} scenario_source_setup_link_t;

typedef struct scenario_source_match {
	const catalog_stage_result_t *stage;
	s32 desired_mode;
	const asset_entry_t *category_exact;
	const asset_entry_t *exact;
	const asset_entry_t *category_fallback;
	const asset_entry_t *fallback;
} scenario_source_match_t;

typedef struct scenario_source_graph_state {
	char scenario_id[CATALOG_ID_LEN];
	char level_graph_path[FS_MAXPATH + 1];
	char mission_id[CATALOG_ID_LEN];
	char mission_graph_path[FS_MAXPATH + 1];
	char mission_objectives_path[FS_MAXPATH + 1];
	char pads_path[FS_MAXPATH + 1];
	char spawns_path[FS_MAXPATH + 1];
	char objects_path[FS_MAXPATH + 1];
	char setup_fields_path[FS_MAXPATH + 1];
	char ai_lists_path[FS_MAXPATH + 1];
	char volumes_path[FS_MAXPATH + 1];
	char objectives_path[FS_MAXPATH + 1];
	char waypoints_path[FS_MAXPATH + 1];
	char waygroups_path[FS_MAXPATH + 1];
	char covers_path[FS_MAXPATH + 1];
	char paths_path[FS_MAXPATH + 1];
	u32 level_graph_size;
	u32 mission_graph_size;
	scenario_source_setup_link_t *setup_links;
	s32 setup_link_count;
	scenario_source_volume_row_t *level_volumes;
	s32 level_volume_count;
	s32 level_volume_node_count;
	s32 level_pad_node_count;
	s32 level_global_settings_node_count;
	s32 level_ai_list_node_count;
	s32 level_ai_stop_node_count;
	s32 level_ai_kneel_node_count;
	s32 level_ai_surrender_node_count;
	s32 level_ai_fade_out_node_count;
	s32 level_ai_remove_chr_node_count;
	s32 level_ai_try_sidestep_node_count;
	s32 level_ai_try_jump_out_node_count;
	s32 level_ai_try_run_sideways_node_count;
	s32 level_ai_try_attack_walk_node_count;
	s32 level_ai_try_attack_run_node_count;
	s32 level_ai_try_attack_roll_node_count;
	s32 level_ai_try_attack_stand_node_count;
	s32 level_ai_try_attack_kneel_node_count;
	s32 level_ai_try_attack_lie_node_count;
	s32 level_ai_if_attack_locked_node_count;
	s32 level_ai_if_attacking_node_count;
	s32 level_ai_try_modify_attack_node_count;
	s32 level_ai_face_entity_node_count;
	s32 level_ai_apply_gset_damage_node_count;
	s32 level_ai_chr_damage_chr_node_count;
	s32 level_ai_consider_grenade_throw_node_count;
	s32 level_ai_drop_item_node_count;
	s32 level_ai_try_run_from_target_node_count;
	s32 level_ai_try_jog_to_target_prop_node_count;
	s32 level_ai_try_walk_to_target_prop_node_count;
	s32 level_ai_try_run_to_target_prop_node_count;
	s32 level_ai_try_go_to_cover_prop_node_count;
	s32 level_ai_try_jog_to_chr_node_count;
	s32 level_ai_try_walk_to_chr_node_count;
	s32 level_ai_try_run_to_chr_node_count;
	s32 level_ai_if_can_hear_alarm_node_count;
	s32 level_ai_if_patrolling_node_count;
	s32 level_ai_if_alarm_active_node_count;
	s32 level_ai_if_gas_active_node_count;
	s32 level_ai_if_hears_target_node_count;
	s32 level_ai_if_saw_injury_node_count;
	s32 level_ai_if_saw_death_node_count;
	s32 level_ai_if_los_to_target_node_count;
	s32 level_ai_if_los_to_attack_target_node_count;
	s32 level_ai_if_target_nearly_in_sight_node_count;
	s32 level_ai_if_nearly_in_targets_sight_node_count;
	s32 level_ai_set_pad_preset_to_pad_on_route_to_target_node_count;
	s32 level_ai_if_saw_target_recently_node_count;
	s32 level_ai_if_heard_target_recently_node_count;
	s32 level_ai_if_los_to_chr_node_count;
	s32 level_ai_if_never_been_on_screen_node_count;
	s32 level_ai_if_on_screen_node_count;
	s32 level_ai_if_chr_in_on_screen_room_node_count;
	s32 level_ai_if_room_is_on_screen_node_count;
	s32 level_ai_if_target_aiming_at_me_node_count;
	s32 level_ai_if_near_miss_node_count;
	s32 level_ai_if_sees_suspicious_item_node_count;
	s32 level_ai_if_check_fov_with_target_node_count;
	s32 level_ai_if_target_in_fov_left_node_count;
	s32 level_ai_if_target_out_of_fov_left_node_count;
	s32 level_ai_if_target_in_fov_node_count;
	s32 level_ai_if_target_out_of_fov_node_count;
	s32 level_ai_if_distance_to_target_less_than_node_count;
	s32 level_ai_if_distance_to_target_greater_than_node_count;
	s32 level_ai_if_chr_distance_to_pad_less_than_node_count;
	s32 level_ai_if_chr_distance_to_pad_greater_than_node_count;
	s32 level_ai_if_distance_to_chr_less_than_node_count;
	s32 level_ai_if_distance_to_chr_greater_than_node_count;
	s32 level_ai_if_any_chr_near_self_node_count;
	s32 level_ai_if_distance_from_target_to_pad_less_than_node_count;
	s32 level_ai_if_distance_from_target_to_pad_greater_than_node_count;
	s32 level_ai_if_chr_in_room_node_count;
	s32 level_ai_if_target_in_room_node_count;
	s32 level_ai_if_chr_has_object_node_count;
	s32 level_ai_if_weapon_thrown_node_count;
	s32 level_ai_if_weapon_thrown_on_object_node_count;
	s32 level_ai_if_chr_has_weapon_equipped_node_count;
	s32 level_ai_if_gun_unclaimed_node_count;
	s32 level_ai_if_object_healthy_node_count;
	s32 level_ai_if_chr_activated_object_node_count;
	s32 level_ai_obj_interact_node_count;
	s32 level_ai_destroy_object_node_count;
	s32 level_ai_drop_object_from_chr_node_count;
	s32 level_ai_chr_drop_items_node_count;
	s32 level_ai_chr_drop_weapon_node_count;
	s32 level_ai_give_object_to_chr_node_count;
	s32 level_ai_object_move_to_pad_node_count;
	s32 level_ai_chr_do_animation_node_count;
	s32 level_ai_be_surprised_one_hand_node_count;
	s32 level_ai_be_surprised_look_around_node_count;
	s32 level_ai_be_surprised_surrender_node_count;
	s32 level_ai_random_node_count;
	s32 level_ai_if_random_less_than_node_count;
	s32 level_ai_if_random_greater_than_node_count;
	s32 level_ai_print_node_count;
	s32 level_ai_noop_node_count;
	s32 level_ai_set_list_node_count;
	s32 level_ai_set_return_list_node_count;
	s32 level_ai_set_shot_list_node_count;
	s32 level_ai_return_list_node_count;
	s32 level_ai_set_punch_dodge_list_node_count;
	s32 level_ai_set_shooting_at_me_list_node_count;
	s32 level_ai_set_dark_room_list_node_count;
	s32 level_ai_set_player_dead_list_node_count;
	s32 level_path_node_count;
	s32 level_ai_jog_to_pad_node_count;
	s32 level_ai_goto_pad_preset_node_count;
	s32 level_ai_walk_to_pad_node_count;
	s32 level_ai_run_to_pad_node_count;
	s32 level_ai_set_path_node_count;
	s32 level_ai_start_patrol_node_count;
	s32 level_ai_set_pad_preset_node_count;
	s32 level_ai_chr_set_pad_preset_node_count;
	s32 level_ai_chr_copy_pad_preset_node_count;
	s32 level_ai_set_chr_preset_node_count;
	s32 level_ai_set_chr_target_node_count;
	s32 level_ai_set_morale_node_count;
	s32 level_ai_add_morale_node_count;
	s32 level_ai_chr_add_morale_node_count;
	s32 level_ai_subtract_morale_node_count;
	s32 level_ai_set_alertness_node_count;
	s32 level_ai_add_alertness_node_count;
	s32 level_ai_chr_add_alertness_node_count;
	s32 level_ai_subtract_alertness_node_count;
	s32 level_ai_if_num_arghs_less_than_node_count;
	s32 level_ai_if_num_arghs_greater_than_node_count;
	s32 level_ai_if_num_close_arghs_less_than_node_count;
	s32 level_ai_if_num_close_arghs_greater_than_node_count;
	s32 level_ai_if_chr_health_greater_than_node_count;
	s32 level_ai_if_chr_health_less_than_node_count;
	s32 level_ai_if_chr_shield_less_than_node_count;
	s32 level_ai_if_chr_shield_greater_than_node_count;
	s32 level_ai_if_injured_node_count;
	s32 level_ai_if_shield_damaged_node_count;
	s32 level_ai_if_morale_less_than_node_count;
	s32 level_ai_if_morale_less_than_random_node_count;
	s32 level_ai_if_alertness_node_count;
	s32 level_ai_if_chr_alertness_less_than_node_count;
	s32 level_ai_if_alertness_less_than_random_node_count;
	s32 level_ai_if_idle_node_count;
	s32 level_ai_if_stopped_node_count;
	s32 level_ai_if_chr_dead_node_count;
	s32 level_ai_if_chr_death_animation_finished_node_count;
	s32 level_ai_if_chr_knocked_out_node_count;
	s32 level_ai_if_can_see_target_node_count;
	s32 level_ai_increase_squadron_alertness_node_count;
	s32 level_ai_set_hear_distance_node_count;
	s32 level_ai_set_view_distance_node_count;
	s32 level_ai_set_grenade_probability_node_count;
	s32 level_ai_set_chr_num_node_count;
	s32 level_ai_set_max_damage_node_count;
	s32 level_ai_add_health_node_count;
	s32 level_ai_set_shield_node_count;
	s32 level_ai_set_reaction_speed_node_count;
	s32 level_ai_set_recovery_speed_node_count;
	s32 level_ai_set_accuracy_node_count;
	s32 level_ai_set_dodge_rating_node_count;
	s32 level_ai_set_unarmed_dodge_rating_node_count;
	s32 level_ai_set_action_node_count;
	s32 level_ai_set_team_orders_node_count;
	s32 level_ai_retreat_node_count;
	s32 level_ai_find_cover_node_count;
	s32 level_ai_find_cover_within_dist_node_count;
	s32 level_ai_find_cover_outside_dist_node_count;
	s32 level_ai_go_to_cover_node_count;
	s32 level_ai_check_cover_out_of_sight_node_count;
	s32 level_ai_orbit_target_node_count;
	s32 level_ai_set_chr_preset_to_unalerted_teammate_node_count;
	s32 level_ai_set_squadron_node_count;
	s32 level_ai_face_cover_node_count;
	s32 level_ai_danger_cover_node_count;
	s32 level_ai_release_cover_node_count;
	s32 level_ai_rebuild_teams_node_count;
	s32 level_ai_rebuild_squadrons_node_count;
	s32 level_ai_chr_set_listening_node_count;
	s32 level_ai_if_chr_not_talking_node_count;
	s32 level_ai_if_orders_node_count;
	s32 level_ai_if_has_orders_node_count;
	s32 level_ai_if_chr_in_squadron_doing_action_node_count;
	s32 level_ai_if_chr_listening_node_count;
	s32 level_ai_if_not_listening_node_count;
	s32 level_ai_if_chr_injured_target_node_count;
	s32 level_ai_if_action_node_count;
	s32 level_ai_if_chr_ammo_quantity_less_than_node_count;
	s32 level_ai_if_chr_target_node_count;
	s32 level_ai_if_compare_chr_presets_team_node_count;
	s32 level_ai_if_human_node_count;
	s32 level_ai_if_skedar_node_count;
	s32 level_ai_if_prop_preset_blocking_sight_to_target_node_count;
	s32 level_ai_remove_object_at_prop_preset_node_count;
	s32 level_ai_if_prop_preset_height_less_than_node_count;
	s32 level_ai_set_target_node_count;
	s32 level_ai_if_presets_target_is_not_my_target_node_count;
	s32 level_ai_set_chr_preset_to_chr_near_self_node_count;
	s32 level_ai_set_chr_preset_to_chr_near_pad_node_count;
	s32 level_ai_if_dangerous_object_nearby_node_count;
	s32 level_ai_if_heli_weapons_armed_node_count;
	s32 level_ai_if_hoverbot_next_step_node_count;
	s32 level_ai_shuffle_investigation_terminals_node_count;
	s32 level_ai_set_pad_preset_to_investigation_terminal_node_count;
	s32 level_ai_heli_arm_weapons_node_count;
	s32 level_ai_heli_unarm_weapons_node_count;
	s32 level_ai_if_safety2_less_than_node_count;
	s32 level_ai_if_player_using_cmp_or_ar34_node_count;
	s32 level_ai_detect_enemy_on_same_floor_node_count;
	s32 level_ai_detect_enemy_node_count;
	s32 level_ai_if_safety_less_than_node_count;
	s32 level_ai_if_target_moving_slowly_node_count;
	s32 level_ai_if_target_moving_closer_node_count;
	s32 level_ai_if_target_moving_away_node_count;
	s32 level_ai_if_squadron_is_dead_node_count;
	s32 level_ai_if_true_node_count;
	s32 level_ai_if_num_chrs_in_squadron_greater_than_node_count;
	s32 level_ai_if_natural_anim_node_count;
	s32 level_ai_if_y_node_count;
	s32 level_ai_if_sound_timer_node_count;
	s32 level_ai_if_target_y_difference_less_than_node_count;
	s32 level_ai_try_attack_amount_node_count;
	s32 level_ai_try_start_alarm_node_count;
	s32 level_ai_activate_alarm_node_count;
	s32 level_ai_deactivate_alarm_node_count;
	s32 level_ai_set_flag_node_count;
	s32 level_ai_unset_flag_node_count;
	s32 level_ai_if_has_flag_node_count;
	s32 level_ai_chr_set_flag_node_count;
	s32 level_ai_chr_unset_flag_node_count;
	s32 level_ai_if_chr_has_flag_node_count;
	s32 level_ai_set_stage_flag_node_count;
	s32 level_ai_unset_stage_flag_node_count;
	s32 level_ai_if_stage_flag_eq_node_count;
	s32 level_ai_set_chrflag_node_count;
	s32 level_ai_unset_chrflag_node_count;
	s32 level_ai_if_has_chrflag_node_count;
	s32 level_ai_chr_set_chrflag_node_count;
	s32 level_ai_chr_unset_chrflag_node_count;
	s32 level_ai_if_chr_has_chrflag_node_count;
	s32 level_ai_chr_set_hidden_flag_node_count;
	s32 level_ai_chr_unset_hidden_flag_node_count;
	s32 level_ai_if_chr_has_hidden_flag_node_count;
	s32 level_ai_set_obj_flag_node_count;
	s32 level_ai_unset_obj_flag_node_count;
	s32 level_ai_if_obj_has_flag_node_count;
	s32 level_ai_open_door_node_count;
	s32 level_ai_close_door_node_count;
	s32 level_ai_if_door_state_node_count;
	s32 level_ai_if_object_is_door_node_count;
	s32 level_ai_lock_door_node_count;
	s32 level_ai_unlock_door_node_count;
	s32 level_ai_if_door_locked_node_count;
	s32 level_ai_if_lift_stationary_node_count;
	s32 level_ai_lift_go_to_stop_node_count;
	s32 level_ai_if_lift_at_stop_node_count;
	s32 level_ai_activate_lift_node_count;
	s32 level_ai_if_using_lift_node_count;
	s32 level_ai_configure_rain_node_count;
	s32 level_ai_configure_snow_node_count;
	s32 level_ai_switch_to_alt_sky_node_count;
	s32 level_ai_set_wind_speed_node_count;
	s32 level_ai_set_lights_node_count;
	s32 level_ai_set_room_flag_node_count;
	s32 level_ai_show_cutscene_chrs_node_count;
	s32 level_ai_configure_environment_node_count;
	s32 level_ai_if_distance_to_target2_less_than_node_count;
	s32 level_ai_if_distance_to_target2_greater_than_node_count;
	s32 level_ai_speak_node_count;
	s32 level_ai_play_sound_node_count;
	s32 level_ai_assign_sound_node_count;
	s32 level_ai_audio_mute_channel_node_count;
	s32 level_ai_if_channel_free_node_count;
	s32 level_ai_set_object_sound_volume_node_count;
	s32 level_ai_set_object_sound_volume_by_distance_node_count;
	s32 level_ai_set_object_sound_playing_node_count;
	s32 level_ai_play_repeating_sound_from_object_node_count;
	s32 level_ai_play_sound_from_entity_node_count;
	s32 level_ai_play_repeating_sound_from_pad_node_count;
	s32 level_ai_if_object_sound_volume_less_than_node_count;
	s32 level_ai_play_sound_from_prop_node_count;
	s32 level_ai_play_temporary_primary_track_node_count;
	s32 level_ai_play_x_track_node_count;
	s32 level_ai_stop_x_track_node_count;
	s32 level_ai_play_track_isolated_node_count;
	s32 level_ai_play_default_tracks_node_count;
	s32 level_ai_play_cutscene_track_node_count;
	s32 level_ai_stop_cutscene_track_node_count;
	s32 level_ai_play_temporary_track_node_count;
	s32 level_ai_stop_ambient_track_node_count;
	s32 level_ai_chr_draw_weapon_node_count;
	s32 level_ai_chr_draw_weapon_in_cutscene_node_count;
	s32 level_ai_set_player_force_speed_node_count;
	s32 level_ai_chr_set_invincible_node_count;
	s32 level_ai_if_player_is_invincible_node_count;
	s32 level_ai_if_chr_has_no_gun_node_count;
	s32 level_ai_chr_delete_weapon_node_count;
	s32 level_ai_if_trigger_shot_list_node_count;
	s32 level_ai_end_level_node_count;
	s32 level_ai_end_cutscene_node_count;
	s32 level_ai_warp_jo_to_pad_node_count;
	s32 level_ai_set_camera_animation_node_count;
	s32 level_ai_if_in_cutscene_node_count;
	s32 level_ai_if_cutscene_button_pressed_node_count;
	s32 level_ai_reorient_for_cutscene_stop_node_count;
	s32 level_ai_warp_jo_to_tag_node_count;
	s32 level_ai_revoke_control_node_count;
	s32 level_ai_grant_control_node_count;
	s32 level_ai_player_fade_in_node_count;
	s32 level_ai_players_fade_out_node_count;
	s32 level_ai_if_colour_fade_complete_node_count;
	s32 level_ai_prepare_warp_orbit_node_count;
	s32 level_ai_begin_warp_latch_node_count;
	s32 level_ai_if_warp_latch_complete_node_count;
	s32 level_ai_spawn_chr_at_pad_node_count;
	s32 level_ai_spawn_chr_at_chr_node_count;
	s32 level_ai_try_equip_weapon_node_count;
	s32 level_ai_try_equip_hat_node_count;
	s32 level_ai_set_obj_image_node_count;
	s32 level_ai_object_do_animation_node_count;
	s32 level_ai_set_door_open_node_count;
	s32 level_ai_duplicate_chr_node_count;
	s32 level_ai_enable_chr_node_count;
	s32 level_ai_disable_chr_node_count;
	s32 level_ai_enable_obj_node_count;
	s32 level_ai_disable_obj_node_count;
	s32 level_ai_chr_move_to_pad_node_count;
	s32 level_ai_chr_set_team_node_count;
	s32 level_ai_damage_chr_by_amount_node_count;
	s32 level_ai_do_preset_animation_node_count;
	s32 level_ai_if_player_chr_portal_distance_less_than_node_count;
	s32 level_ai_if_chr_reposition_valid_node_count;
	s32 level_ai_do_gun_command_node_count;
	s32 level_ai_if_distance_to_gun_less_than_node_count;
	s32 level_ai_recover_gun_node_count;
	s32 level_ai_chr_copy_properties_node_count;
	s32 level_ai_player_auto_walk_node_count;
	s32 level_ai_if_player_auto_walk_finished_node_count;
	s32 level_ai_if_obj_in_room_node_count;
	s32 level_ai_if_player_looking_at_object_node_count;
	s32 level_ai_if_target_is_player_node_count;
	s32 level_ai_chr_kill_node_count;
	s32 level_ai_remove_weapon_from_inventory_node_count;
	s32 level_ai_clear_inventory_node_count;
	s32 level_ai_release_object_node_count;
	s32 level_ai_chr_grab_object_node_count;
	s32 level_ai_toggle_p1p2_node_count;
	s32 level_ai_chr_set_p1p2_node_count;
	s32 level_ai_chr_set_cloaked_node_count;
	s32 level_ai_set_autogun_target_team_node_count;
	s32 level_ai_if_objective_complete_node_count;
	s32 level_ai_if_objective_failed_node_count;
	s32 level_ai_if_all_objectives_complete_node_count;
	s32 level_ai_if_difficulty_less_than_node_count;
	s32 level_ai_if_difficulty_greater_than_node_count;
	s32 level_ai_if_stage_timer_less_than_node_count;
	s32 level_ai_if_stage_timer_greater_than_node_count;
	s32 level_ai_if_stage_id_less_than_node_count;
	s32 level_ai_if_stage_id_greater_than_node_count;
	s32 level_ai_if_waypoint_within_quadrant_node_count;
	s32 level_ai_set_pad_preset_to_target_quadrant_node_count;
	s32 level_ai_if_num_players_less_than_node_count;
	s32 level_ai_if_kill_count_greater_than_node_count;
	s32 level_ai_if_num_knocked_out_chrs_node_count;
	s32 level_ai_kill_bond_node_count;
	s32 level_ai_if_pouncebits_eq_node_count;
	s32 level_ai_if_training_pc_holographed_node_count;
	s32 level_ai_if_player_using_device_node_count;
	s32 level_ai_chr_begin_or_end_teleport_node_count;
	s32 level_ai_if_chr_teleport_full_white_node_count;
	s32 level_ai_chr_set_cutscene_weapon_node_count;
	s32 level_ai_fade_screen_node_count;
	s32 level_ai_if_fade_complete_node_count;
	s32 level_ai_set_chr_hudpiece_visible_node_count;
	s32 level_ai_set_passive_mode_node_count;
	s32 level_ai_chr_set_firing_in_cutscene_node_count;
	s32 level_ai_set_portal_flag_node_count;
	s32 level_ai_if_music_event_queue_is_empty_node_count;
	s32 level_ai_if_coop_mode_node_count;
	s32 level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count;
	s32 level_ai_remove_references_to_chr_node_count;
	s32 level_ai_chr_toggle_model_part_node_count;
	s32 level_ai_obj_set_model_part_visible_node_count;
	s32 level_ai_if_obj_health_less_than_node_count;
	s32 level_ai_set_obj_health_node_count;
	s32 level_ai_set_chr_special_death_animation_node_count;
	s32 level_ai_set_room_to_search_node_count;
	s32 level_ai_restart_timer_node_count;
	s32 level_ai_reset_timer_node_count;
	s32 level_ai_pause_timer_node_count;
	s32 level_ai_resume_timer_node_count;
	s32 level_ai_if_timer_stopped_node_count;
	s32 level_ai_if_timer_greater_than_random_node_count;
	s32 level_ai_if_timer_less_than_node_count;
	s32 level_ai_if_timer_greater_than_node_count;
	s32 level_ai_show_countdown_timer_node_count;
	s32 level_ai_hide_countdown_timer_node_count;
	s32 level_ai_set_countdown_timer_node_count;
	s32 level_ai_stop_countdown_timer_node_count;
	s32 level_ai_start_countdown_timer_node_count;
	s32 level_ai_if_countdown_timer_stopped_node_count;
	s32 level_ai_if_countdown_timer_less_than_node_count;
	s32 level_ai_if_countdown_timer_greater_than_node_count;
	s32 level_ai_set_savefile_flag_node_count;
	s32 level_ai_unset_savefile_flag_node_count;
	s32 level_ai_if_savefile_flag_set_node_count;
	s32 level_ai_if_savefile_flag_unset_node_count;
	s32 level_ai_show_hudmsg_node_count;
	s32 level_ai_show_hudmsg_middle_node_count;
	s32 level_ai_show_hudmsg_top_middle_node_count;
	s32 level_ai_hovercar_begin_path_node_count;
	s32 level_ai_set_vehicle_speed_node_count;
	s32 level_ai_set_rotor_speed_node_count;
	s32 level_ai_chr_explosions_node_count;
	s32 level_ai_set_tinted_glass_enabled_node_count;
	s32 level_ai_hovercopter_fire_rocket_node_count;
	s32 level_ai_chr_adjust_motion_blur_node_count;
	s32 level_ai_punch_or_kick_node_count;
	s32 level_ai_set_target_to_eyespy_if_in_sight_node_count;
	s32 level_ai_mini_skedar_try_pounce_node_count;
	s32 level_ai_if_object_distance_to_pad_less_than_node_count;
	s32 level_ai_avoid_node_count;
	s32 level_ai_title_init_mode_node_count;
	s32 level_ai_try_exit_title_node_count;
	s32 level_ai_chr_emit_sparks_node_count;
	s32 level_ai_set_dr_caroll_images_node_count;
	s32 level_ai_say_quip_node_count;
	s32 level_ai_say_ci_staff_quip_node_count;
	s32 level_ai_shuffle_ruins_pillars_node_count;
	s32 level_ai_shuffle_pelagic_switches_node_count;
	char level_global_settings_scenario[CATALOG_ID_LEN];
	char level_global_settings_kind[32];
	scenario_source_objective_node_t *mission_objectives;
	scenario_source_objective_criteria_t *mission_objective_criteria;
	s32 mission_objective_nodes;
	s32 mission_objective_criteria_nodes;
	s32 level_graph_active;
	s32 mission_graph_active;
	s32 mission_objective_runtime_active;
	s32 mission_objective_insert_logged;
	s32 mission_objective_evaluate_logged;
	s32 mission_objective_check_logged;
	s32 mission_objective_state_logged;
	s32 mission_objective_object_state_logged;
	s32 setup_link_logged;
	s32 ai_action_stop_logged;
	s32 ai_action_kneel_logged;
	s32 ai_action_surrender_logged;
	s32 ai_action_fade_out_logged;
	s32 ai_action_remove_chr_logged;
	s32 ai_action_try_sidestep_logged;
	s32 ai_action_try_jump_out_logged;
	s32 ai_action_try_run_sideways_logged;
	s32 ai_action_try_attack_walk_logged;
	s32 ai_action_try_attack_run_logged;
	s32 ai_action_try_attack_roll_logged;
	s32 ai_action_try_attack_stand_logged;
	s32 ai_action_try_attack_kneel_logged;
	s32 ai_action_try_attack_lie_logged;
	s32 ai_condition_if_attack_locked_logged;
	s32 ai_condition_if_attacking_logged;
	s32 ai_action_try_modify_attack_logged;
	s32 ai_action_face_entity_logged;
	s32 ai_action_apply_gset_damage_logged;
	s32 ai_action_chr_damage_chr_logged;
	s32 ai_condition_consider_grenade_throw_logged;
	s32 ai_action_drop_item_logged;
	s32 ai_action_try_run_from_target_logged;
	s32 ai_action_try_jog_to_target_prop_logged;
	s32 ai_action_try_walk_to_target_prop_logged;
	s32 ai_action_try_run_to_target_prop_logged;
	s32 ai_action_try_go_to_cover_prop_logged;
	s32 ai_action_try_jog_to_chr_logged;
	s32 ai_action_try_walk_to_chr_logged;
	s32 ai_action_try_run_to_chr_logged;
	s32 ai_condition_if_can_hear_alarm_logged;
	s32 ai_condition_if_patrolling_logged;
	s32 ai_condition_if_alarm_active_logged;
	s32 ai_condition_if_gas_active_logged;
	s32 ai_condition_if_hears_target_logged;
	s32 ai_condition_if_saw_injury_logged;
	s32 ai_condition_if_saw_death_logged;
	s32 ai_condition_if_los_to_target_logged;
	s32 ai_condition_if_los_to_attack_target_logged;
	s32 ai_condition_if_target_nearly_in_sight_logged;
	s32 ai_condition_if_nearly_in_targets_sight_logged;
	s32 ai_action_set_pad_preset_to_pad_on_route_to_target_logged;
	s32 ai_condition_if_saw_target_recently_logged;
	s32 ai_condition_if_heard_target_recently_logged;
	s32 ai_condition_if_los_to_chr_logged;
	s32 ai_condition_if_never_been_on_screen_logged;
	s32 ai_condition_if_on_screen_logged;
	s32 ai_condition_if_chr_in_on_screen_room_logged;
	s32 ai_condition_if_room_is_on_screen_logged;
	s32 ai_condition_if_target_aiming_at_me_logged;
	s32 ai_condition_if_near_miss_logged;
	s32 ai_condition_if_sees_suspicious_item_logged;
	s32 ai_condition_if_check_fov_with_target_logged;
	s32 ai_condition_if_target_in_fov_left_logged;
	s32 ai_condition_if_target_out_of_fov_left_logged;
	s32 ai_condition_if_target_in_fov_logged;
	s32 ai_condition_if_target_out_of_fov_logged;
	s32 ai_condition_if_distance_to_target_less_than_logged;
	s32 ai_condition_if_distance_to_target_greater_than_logged;
	s32 ai_condition_if_chr_distance_to_pad_less_than_logged;
	s32 ai_condition_if_chr_distance_to_pad_greater_than_logged;
	s32 ai_condition_if_distance_to_chr_less_than_logged;
	s32 ai_condition_if_distance_to_chr_greater_than_logged;
	s32 ai_condition_if_any_chr_near_self_logged;
	s32 ai_condition_if_distance_from_target_to_pad_less_than_logged;
	s32 ai_condition_if_distance_from_target_to_pad_greater_than_logged;
	s32 ai_condition_if_chr_in_room_logged;
	s32 ai_condition_if_target_in_room_logged;
	s32 ai_condition_if_chr_has_object_logged;
	s32 ai_condition_if_weapon_thrown_logged;
	s32 ai_condition_if_weapon_thrown_on_object_logged;
	s32 ai_condition_if_chr_has_weapon_equipped_logged;
	s32 ai_condition_if_gun_unclaimed_logged;
	s32 ai_condition_if_object_healthy_logged;
	s32 ai_condition_if_chr_activated_object_logged;
	s32 ai_action_obj_interact_logged;
	s32 ai_action_destroy_object_logged;
	s32 ai_action_drop_object_from_chr_logged;
	s32 ai_action_chr_drop_items_logged;
	s32 ai_action_chr_drop_weapon_logged;
	s32 ai_action_give_object_to_chr_logged;
	s32 ai_action_object_move_to_pad_logged;
	s32 ai_action_chr_do_animation_logged;
	s32 ai_action_be_surprised_one_hand_logged;
	s32 ai_action_be_surprised_look_around_logged;
	s32 ai_action_be_surprised_surrender_logged;
	s32 ai_action_random_logged;
	s32 ai_condition_if_random_less_than_logged;
	s32 ai_condition_if_random_greater_than_logged;
	s32 ai_action_print_logged;
	s32 ai_action_noop_logged;
	s32 ai_action_set_list_logged;
	s32 ai_action_set_return_list_logged;
	s32 ai_action_set_shot_list_logged;
	s32 ai_action_return_list_logged;
	s32 ai_action_set_punch_dodge_list_logged;
	s32 ai_action_set_shooting_at_me_list_logged;
	s32 ai_action_set_dark_room_list_logged;
	s32 ai_action_set_player_dead_list_logged;
	s32 ai_action_jog_to_pad_logged;
	s32 ai_action_goto_pad_preset_logged;
	s32 ai_action_walk_to_pad_logged;
	s32 ai_action_run_to_pad_logged;
	s32 ai_action_set_path_logged;
	s32 ai_action_start_patrol_logged;
	s32 ai_action_set_pad_preset_logged;
	s32 ai_action_chr_set_pad_preset_logged;
	s32 ai_action_chr_copy_pad_preset_logged;
	s32 ai_action_set_chr_preset_logged;
	s32 ai_action_set_chr_target_logged;
	s32 ai_action_set_morale_logged;
	s32 ai_action_add_morale_logged;
	s32 ai_action_chr_add_morale_logged;
	s32 ai_action_subtract_morale_logged;
	s32 ai_action_set_alertness_logged;
	s32 ai_action_add_alertness_logged;
	s32 ai_action_chr_add_alertness_logged;
	s32 ai_action_subtract_alertness_logged;
	s32 ai_condition_if_num_arghs_less_than_logged;
	s32 ai_condition_if_num_arghs_greater_than_logged;
	s32 ai_condition_if_num_close_arghs_less_than_logged;
	s32 ai_condition_if_num_close_arghs_greater_than_logged;
	s32 ai_condition_if_chr_health_greater_than_logged;
	s32 ai_condition_if_chr_health_less_than_logged;
	s32 ai_condition_if_chr_shield_less_than_logged;
	s32 ai_condition_if_chr_shield_greater_than_logged;
	s32 ai_condition_if_injured_logged;
	s32 ai_condition_if_shield_damaged_logged;
	s32 ai_condition_if_morale_less_than_logged;
	s32 ai_condition_if_morale_less_than_random_logged;
	s32 ai_condition_if_alertness_logged;
	s32 ai_condition_if_chr_alertness_less_than_logged;
	s32 ai_condition_if_alertness_less_than_random_logged;
	s32 ai_condition_if_idle_logged;
	s32 ai_condition_if_stopped_logged;
	s32 ai_condition_if_chr_dead_logged;
	s32 ai_condition_if_chr_death_animation_finished_logged;
	s32 ai_condition_if_chr_knocked_out_logged;
	s32 ai_condition_if_can_see_target_logged;
	s32 ai_action_increase_squadron_alertness_logged;
	s32 ai_action_set_hear_distance_logged;
	s32 ai_action_set_view_distance_logged;
	s32 ai_action_set_grenade_probability_logged;
	s32 ai_action_set_chr_num_logged;
	s32 ai_action_set_max_damage_logged;
	s32 ai_action_add_health_logged;
	s32 ai_action_set_shield_logged;
	s32 ai_action_set_reaction_speed_logged;
	s32 ai_action_set_recovery_speed_logged;
	s32 ai_action_set_accuracy_logged;
	s32 ai_action_set_dodge_rating_logged;
	s32 ai_action_set_unarmed_dodge_rating_logged;
	s32 ai_action_set_action_logged;
	s32 ai_action_set_team_orders_logged;
	s32 ai_action_retreat_logged;
	s32 ai_action_find_cover_logged;
	s32 ai_action_find_cover_within_dist_logged;
	s32 ai_action_find_cover_outside_dist_logged;
	s32 ai_action_go_to_cover_logged;
	s32 ai_action_check_cover_out_of_sight_logged;
	s32 ai_action_orbit_target_logged;
	s32 ai_action_set_chr_preset_to_unalerted_teammate_logged;
	s32 ai_action_set_squadron_logged;
	s32 ai_action_face_cover_logged;
	s32 ai_action_danger_cover_logged;
	s32 ai_action_release_cover_logged;
	s32 ai_action_rebuild_teams_logged;
	s32 ai_action_rebuild_squadrons_logged;
	s32 ai_action_chr_set_listening_logged;
	s32 ai_condition_if_chr_not_talking_logged;
	s32 ai_condition_if_orders_logged;
	s32 ai_condition_if_has_orders_logged;
	s32 ai_condition_if_chr_in_squadron_doing_action_logged;
	s32 ai_condition_if_chr_listening_logged;
	s32 ai_condition_if_not_listening_logged;
	s32 ai_condition_if_chr_injured_target_logged;
	s32 ai_condition_if_action_logged;
	s32 ai_condition_if_chr_ammo_quantity_less_than_logged;
	s32 ai_condition_if_chr_target_logged;
	s32 ai_condition_if_compare_chr_presets_team_logged;
	s32 ai_condition_if_human_logged;
	s32 ai_condition_if_skedar_logged;
	s32 ai_condition_if_prop_preset_blocking_sight_to_target_logged;
	s32 ai_action_remove_object_at_prop_preset_logged;
	s32 ai_condition_if_prop_preset_height_less_than_logged;
	s32 ai_action_set_target_logged;
	s32 ai_condition_if_presets_target_is_not_my_target_logged;
	s32 ai_action_set_chr_preset_to_chr_near_self_logged;
	s32 ai_action_set_chr_preset_to_chr_near_pad_logged;
	s32 ai_condition_if_dangerous_object_nearby_logged;
	s32 ai_condition_if_heli_weapons_armed_logged;
	s32 ai_condition_if_hoverbot_next_step_logged;
	s32 ai_action_shuffle_investigation_terminals_logged;
	s32 ai_action_set_pad_preset_to_investigation_terminal_logged;
	s32 ai_action_heli_arm_weapons_logged;
	s32 ai_action_heli_unarm_weapons_logged;
	s32 ai_condition_if_safety2_less_than_logged;
	s32 ai_condition_if_player_using_cmp_or_ar34_logged;
	s32 ai_condition_detect_enemy_on_same_floor_logged;
	s32 ai_condition_detect_enemy_logged;
	s32 ai_condition_if_safety_less_than_logged;
	s32 ai_condition_if_target_moving_slowly_logged;
	s32 ai_condition_if_target_moving_closer_logged;
	s32 ai_condition_if_target_moving_away_logged;
	s32 ai_condition_if_squadron_is_dead_logged;
	s32 ai_condition_if_true_logged;
	s32 ai_condition_if_num_chrs_in_squadron_greater_than_logged;
	s32 ai_condition_if_natural_anim_logged;
	s32 ai_condition_if_y_logged;
	s32 ai_condition_if_sound_timer_logged;
	s32 ai_condition_if_target_y_difference_less_than_logged;
	s32 ai_action_try_attack_amount_logged;
	s32 ai_action_try_start_alarm_logged;
	s32 ai_action_activate_alarm_logged;
	s32 ai_action_deactivate_alarm_logged;
	s32 ai_action_set_flag_logged;
	s32 ai_action_unset_flag_logged;
	s32 ai_action_if_has_flag_logged;
	s32 ai_action_chr_set_flag_logged;
	s32 ai_action_chr_unset_flag_logged;
	s32 ai_action_if_chr_has_flag_logged;
	s32 ai_action_set_stage_flag_logged;
	s32 ai_action_unset_stage_flag_logged;
	s32 ai_action_if_stage_flag_eq_logged;
	s32 ai_action_set_chrflag_logged;
	s32 ai_action_unset_chrflag_logged;
	s32 ai_action_if_has_chrflag_logged;
	s32 ai_action_chr_set_chrflag_logged;
	s32 ai_action_chr_unset_chrflag_logged;
	s32 ai_action_if_chr_has_chrflag_logged;
	s32 ai_action_chr_set_hidden_flag_logged;
	s32 ai_action_chr_unset_hidden_flag_logged;
	s32 ai_action_if_chr_has_hidden_flag_logged;
	s32 ai_action_set_obj_flag_logged;
	s32 ai_action_unset_obj_flag_logged;
	s32 ai_action_if_obj_has_flag_logged;
	s32 ai_action_open_door_logged;
	s32 ai_action_close_door_logged;
	s32 ai_action_if_door_state_logged;
	s32 ai_action_if_object_is_door_logged;
	s32 ai_action_lock_door_logged;
	s32 ai_action_unlock_door_logged;
	s32 ai_action_if_door_locked_logged;
	s32 ai_action_if_lift_stationary_logged;
	s32 ai_action_lift_go_to_stop_logged;
	s32 ai_action_if_lift_at_stop_logged;
	s32 ai_action_activate_lift_logged;
	s32 ai_action_if_using_lift_logged;
	s32 ai_action_configure_rain_logged;
	s32 ai_action_configure_snow_logged;
	s32 ai_action_switch_to_alt_sky_logged;
	s32 ai_action_set_wind_speed_logged;
	s32 ai_action_set_lights_logged;
	s32 ai_action_set_room_flag_logged;
	s32 ai_action_show_cutscene_chrs_logged;
	s32 ai_action_configure_environment_logged;
	s32 ai_action_if_distance_to_target2_logged;
	s32 ai_action_speak_logged;
	s32 ai_action_play_sound_logged;
	s32 ai_action_assign_sound_logged;
	s32 ai_action_audio_mute_channel_logged;
	s32 ai_condition_if_channel_free_logged;
	s32 ai_action_set_object_sound_volume_logged;
	s32 ai_action_set_object_sound_volume_by_distance_logged;
	s32 ai_action_set_object_sound_playing_logged;
	s32 ai_action_play_repeating_sound_from_object_logged;
	s32 ai_action_play_sound_from_entity_logged;
	s32 ai_action_play_repeating_sound_from_pad_logged;
	s32 ai_condition_if_object_sound_volume_less_than_logged;
	s32 ai_action_play_sound_from_prop_logged;
	s32 ai_action_play_temporary_primary_track_logged;
	s32 ai_action_play_x_track_logged;
	s32 ai_action_stop_x_track_logged;
	s32 ai_action_play_track_isolated_logged;
	s32 ai_action_play_default_tracks_logged;
	s32 ai_action_play_cutscene_track_logged;
	s32 ai_action_stop_cutscene_track_logged;
	s32 ai_action_play_temporary_track_logged;
	s32 ai_action_stop_ambient_track_logged;
	s32 ai_action_chr_draw_weapon_logged;
	s32 ai_action_chr_draw_weapon_in_cutscene_logged;
	s32 ai_action_set_player_force_speed_logged;
	s32 ai_action_chr_set_invincible_logged;
	s32 ai_action_if_player_is_invincible_logged;
	s32 ai_action_if_chr_has_no_gun_logged;
	s32 ai_action_chr_delete_weapon_logged;
	s32 ai_condition_if_trigger_shot_list_logged;
	s32 ai_action_end_level_logged;
	s32 ai_action_end_cutscene_logged;
	s32 ai_action_warp_jo_to_pad_logged;
	s32 ai_action_set_camera_animation_logged;
	s32 ai_condition_if_in_cutscene_logged;
	s32 ai_condition_if_cutscene_button_pressed_logged;
	s32 ai_action_reorient_for_cutscene_stop_logged;
	s32 ai_action_warp_jo_to_tag_logged;
	s32 ai_action_revoke_control_logged;
	s32 ai_action_grant_control_logged;
	s32 ai_action_player_fade_in_logged;
	s32 ai_action_players_fade_out_logged;
	s32 ai_condition_if_colour_fade_complete_logged;
	s32 ai_action_prepare_warp_orbit_logged;
	s32 ai_action_begin_warp_latch_logged;
	s32 ai_condition_if_warp_latch_complete_logged;
	s32 ai_action_spawn_chr_at_pad_logged;
	s32 ai_action_spawn_chr_at_chr_logged;
	s32 ai_action_try_equip_weapon_logged;
	s32 ai_action_try_equip_hat_logged;
	s32 ai_action_set_obj_image_logged;
	s32 ai_action_object_do_animation_logged;
	s32 ai_action_set_door_open_logged;
	s32 ai_action_duplicate_chr_logged;
	s32 ai_action_enable_chr_logged;
	s32 ai_action_disable_chr_logged;
	s32 ai_action_enable_obj_logged;
	s32 ai_action_disable_obj_logged;
	s32 ai_action_chr_move_to_pad_logged;
	s32 ai_action_chr_set_team_logged;
	s32 ai_action_damage_chr_by_amount_logged;
	s32 ai_action_do_preset_animation_logged;
	s32 ai_condition_if_player_chr_portal_distance_less_than_logged;
	s32 ai_condition_if_chr_reposition_valid_logged;
	s32 ai_action_do_gun_command_logged;
	s32 ai_action_if_distance_to_gun_less_than_logged;
	s32 ai_action_recover_gun_logged;
	s32 ai_action_chr_copy_properties_logged;
	s32 ai_action_player_auto_walk_logged;
	s32 ai_condition_if_player_auto_walk_finished_logged;
	s32 ai_action_if_obj_in_room_logged;
	s32 ai_condition_if_player_looking_at_object_logged;
	s32 ai_condition_if_target_is_player_logged;
	s32 ai_action_chr_kill_logged;
	s32 ai_action_remove_weapon_from_inventory_logged;
	s32 ai_action_clear_inventory_logged;
	s32 ai_action_release_object_logged;
	s32 ai_action_chr_grab_object_logged;
	s32 ai_action_toggle_p1p2_logged;
	s32 ai_action_chr_set_p1p2_logged;
	s32 ai_action_chr_set_cloaked_logged;
	s32 ai_action_set_autogun_target_team_logged;
	s32 ai_condition_if_objective_complete_logged;
	s32 ai_condition_if_objective_failed_logged;
	s32 ai_condition_if_all_objectives_complete_logged;
	s32 ai_condition_if_difficulty_less_than_logged;
	s32 ai_condition_if_difficulty_greater_than_logged;
	s32 ai_condition_if_stage_timer_less_than_logged;
	s32 ai_condition_if_stage_timer_greater_than_logged;
	s32 ai_condition_if_stage_id_less_than_logged;
	s32 ai_condition_if_stage_id_greater_than_logged;
	s32 ai_condition_if_waypoint_within_quadrant_logged;
	s32 ai_action_set_pad_preset_to_target_quadrant_logged;
	s32 ai_condition_if_num_players_less_than_logged;
	s32 ai_condition_if_kill_count_greater_than_logged;
	s32 ai_condition_if_num_knocked_out_chrs_logged;
	s32 ai_action_kill_bond_logged;
	s32 ai_condition_if_pouncebits_eq_logged;
	s32 ai_condition_if_training_pc_holographed_logged;
	s32 ai_condition_if_player_using_device_logged;
	s32 ai_action_chr_begin_or_end_teleport_logged;
	s32 ai_condition_if_chr_teleport_full_white_logged;
	s32 ai_action_chr_set_cutscene_weapon_logged;
	s32 ai_action_fade_screen_logged;
	s32 ai_condition_if_fade_complete_logged;
	s32 ai_action_set_chr_hudpiece_visible_logged;
	s32 ai_action_set_passive_mode_logged;
	s32 ai_action_chr_set_firing_in_cutscene_logged;
	s32 ai_action_set_portal_flag_logged;
	s32 ai_action_if_music_event_queue_is_empty_logged;
	s32 ai_action_if_coop_mode_logged;
	s32 ai_action_if_chr_same_floor_distance_to_pad_less_than_logged;
	s32 ai_action_remove_references_to_chr_logged;
	s32 ai_action_chr_toggle_model_part_logged;
	s32 ai_action_obj_set_model_part_visible_logged;
	s32 ai_action_if_obj_health_less_than_logged;
	s32 ai_action_set_obj_health_logged;
	s32 ai_action_set_chr_special_death_animation_logged;
	s32 ai_action_set_room_to_search_logged;
	s32 ai_action_restart_timer_logged;
	s32 ai_action_reset_timer_logged;
	s32 ai_action_pause_timer_logged;
	s32 ai_action_resume_timer_logged;
	s32 ai_action_if_timer_stopped_logged;
	s32 ai_action_if_timer_greater_than_random_logged;
	s32 ai_action_if_timer_less_than_logged;
	s32 ai_action_if_timer_greater_than_logged;
	s32 ai_action_show_countdown_timer_logged;
	s32 ai_action_hide_countdown_timer_logged;
	s32 ai_action_set_countdown_timer_logged;
	s32 ai_action_stop_countdown_timer_logged;
	s32 ai_action_start_countdown_timer_logged;
	s32 ai_action_if_countdown_timer_stopped_logged;
	s32 ai_action_if_countdown_timer_less_than_logged;
	s32 ai_action_if_countdown_timer_greater_than_logged;
	s32 ai_action_set_savefile_flag_logged;
	s32 ai_action_unset_savefile_flag_logged;
	s32 ai_action_if_savefile_flag_set_logged;
	s32 ai_action_if_savefile_flag_unset_logged;
	s32 ai_action_show_hudmsg_logged;
	s32 ai_action_show_hudmsg_middle_logged;
	s32 ai_action_show_hudmsg_top_middle_logged;
	s32 ai_action_hovercar_begin_path_logged;
	s32 ai_action_set_vehicle_speed_logged;
	s32 ai_action_set_rotor_speed_logged;
	s32 ai_action_chr_explosions_logged;
	s32 ai_action_set_tinted_glass_enabled_logged;
	s32 ai_action_hovercopter_fire_rocket_logged;
	s32 ai_action_chr_adjust_motion_blur_logged;
	s32 ai_action_punch_or_kick_logged;
	s32 ai_action_set_target_to_eyespy_if_in_sight_logged;
	s32 ai_action_mini_skedar_try_pounce_logged;
	s32 ai_condition_if_object_distance_to_pad_less_than_logged;
	s32 ai_action_avoid_logged;
	s32 ai_action_title_init_mode_logged;
	s32 ai_action_try_exit_title_logged;
	s32 ai_action_chr_emit_sparks_logged;
	s32 ai_action_set_dr_caroll_images_logged;
	s32 ai_action_say_quip_logged;
	s32 ai_action_say_ci_staff_quip_logged;
	s32 ai_action_shuffle_ruins_pillars_logged;
	s32 ai_action_shuffle_pelagic_switches_logged;
	s32 level_volume_eval_logged;
	s32 level_volume_missing_logged;
	u32 mission_stage_flags;
	s32 mission_stage_flags_valid;
	s32 mission_stage_flags_logged;
	s32 mission_objective_range_warning_logged;
	s32 mission_phase_node_count;
	char mission_phase_state[32];
} scenario_source_graph_state_t;

typedef struct scenario_source_mission_match {
	const asset_entry_t *scenario;
	const asset_entry_t *archive_match;
	const asset_entry_t *graph_match;
	char scenario_slug[CATALOG_ID_LEN];
} scenario_source_mission_match_t;

#define SCENARIO_SEGMENT_FLAG_OUTWARD 0x4000u
#define SCENARIO_SEGMENT_FLAG_INWARD 0x8000u
#define SCENARIO_SEGMENT_ID_MASK 0x3fffu

static scenario_source_graph_state_t s_ActiveScenarioGraphs;

static s32 s_activeGraphPathForScenario(const asset_entry_t *scenario,
	const char *path, char *out, size_t out_n);
static char *s_loadGraphText(const char *path, u32 *out_size);

static s32 s_objectiveCriterionUsesGraphStatus(u8 type)
{
	return type == OBJECTIVETYPE_HOLOGRAPH ||
		type == OBJECTIVETYPE_ENTERROOM ||
		type == OBJECTIVETYPE_THROWINROOM;
}

static s32 s_objectiveCriterionUsesObjectState(u8 type)
{
	return type == OBJECTIVETYPE_DESTROYOBJ ||
		type == OBJECTIVETYPE_COLLECTOBJ ||
		type == OBJECTIVETYPE_THROWOBJ ||
		type == OBJECTIVETYPE_HOLOGRAPH;
}

static void s_resetActiveScenarioGraphs(void)
{
	free(s_ActiveScenarioGraphs.setup_links);
	free(s_ActiveScenarioGraphs.level_volumes);
	free(s_ActiveScenarioGraphs.mission_objectives);
	free(s_ActiveScenarioGraphs.mission_objective_criteria);
	memset(&s_ActiveScenarioGraphs, 0, sizeof(s_ActiveScenarioGraphs));
}

static char *s_nextField(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) {
		return NULL;
	}

	start = *cursor;
	p = start;

	while (*p && *p != '\t' && *p != '\n' && *p != '\r') {
		p++;
	}

	if (*p) {
		*p++ = '\0';
		if (*(p - 1) == '\r' && *p == '\n') {
			p++;
		}
		*cursor = p;
	} else {
		*cursor = NULL;
	}

	return start;
}

static char *s_nextLine(char **cursor)
{
	char *start;
	char *p;

	if (!cursor || !*cursor) {
		return NULL;
	}

	start = *cursor;
	p = start;

	while (*p && *p != '\n') {
		p++;
	}

	if (*p == '\n') {
		*p++ = '\0';
		*cursor = p;
	} else {
		*cursor = NULL;
	}

	if (p > start && p[-1] == '\0' && p - start >= 2 && p[-2] == '\r') {
		p[-2] = '\0';
	}

	return start;
}

static s32 s_startsWith(const char *value, const char *prefix)
{
	size_t prefix_len;

	if (!value || !prefix) {
		return 0;
	}

	prefix_len = strlen(prefix);
	return strncmp(value, prefix, prefix_len) == 0;
}

static void s_copyString(char *out, size_t out_n, const char *value)
{
	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!value || !value[0]) {
		return;
	}
	strncpy(out, value, out_n - 1);
	out[out_n - 1] = '\0';
}

static s32 s_parseRoomRef(const char *value)
{
	const char *p;

	if (!value || !value[0]) {
		return -1;
	}

	p = value;
	while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
		p++;
	}

	if (!*p) {
		return -1;
	}

	return (s32)strtol(p, NULL, 10);
}

static s32 s_parseFloat(const char *value, f32 *out)
{
	char *end;
	double parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtod(value, &end);
	if (end == value) {
		return 0;
	}

	*out = (f32)parsed;
	return 1;
}

static s32 s_parseIndexedRef(const char *value)
{
	const char *p;

	if (!value || !value[0]) {
		return -1;
	}

	p = value;
	while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
		p++;
	}

	if (!*p) {
		return -1;
	}

	return (s32)strtol(p, NULL, 10);
}

static s32 s_parseStageFlagRef(const char *value, u32 *out)
{
	const char *p;

	if (!value || !value[0] || !out) {
		return 0;
	}

	p = strstr(value, "0x");
	if (!p) {
		p = value;
		while (*p && !isxdigit((unsigned char)*p)) {
			p++;
		}
	}
	if (!p || !*p) {
		return 0;
	}

	*out = (u32)strtoul(p, NULL, 0);
	return 1;
}

static s32 s_setupRecordRefOffset(const scenario_source_setup_record_t *record,
	const char *ref_record_id, s32 *out)
{
	s32 target;

	if (!record || !ref_record_id || !ref_record_id[0] || !out) {
		return 0;
	}

	target = s_parseIndexedRef(ref_record_id);
	if (target < 0) {
		return 0;
	}

	*out = target - record->order;
	return 1;
}

static s32 s_parseInteger(const char *value, s32 *out)
{
	char *end;
	long parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtol(value, &end, 0);
	if (end == value) {
		return 0;
	}

	*out = (s32)parsed;
	return 1;
}

static s32 s_parseU32Value(const char *value, u32 *out)
{
	char *end;
	unsigned long parsed;

	if (!value || !out) {
		return 0;
	}

	parsed = strtoul(value, &end, 0);
	if (end == value) {
		return 0;
	}

	*out = (u32)parsed;
	return 1;
}

static s32 s_setupKindToObjType(const char *kind, u8 *out_type)
{
	struct kind_map {
		const char *kind;
		u8 type;
	};
	static const struct kind_map maps[] = {
		{ "door", OBJTYPE_DOOR },
		{ "door_scale", OBJTYPE_DOORSCALE },
		{ "prop", OBJTYPE_BASIC },
		{ "key", OBJTYPE_KEY },
		{ "alarm", OBJTYPE_ALARM },
		{ "cctv", OBJTYPE_CCTV },
		{ "ammo_crate", OBJTYPE_AMMOCRATE },
		{ "weapon_pickup", OBJTYPE_WEAPON },
		{ "character_spawn", OBJTYPE_CHR },
		{ "single_monitor", OBJTYPE_SINGLEMONITOR },
		{ "multi_monitor", OBJTYPE_MULTIMONITOR },
		{ "hanging_monitors", OBJTYPE_HANGINGMONITORS },
		{ "autogun", OBJTYPE_AUTOGUN },
		{ "linked_guns", OBJTYPE_LINKGUNS },
		{ "debris", OBJTYPE_DEBRIS },
		{ "hat", OBJTYPE_HAT },
		{ "grenade_probability", OBJTYPE_GRENADEPROB },
		{ "lift_door_link", OBJTYPE_LINKLIFTDOOR },
		{ "multi_ammo_crate", OBJTYPE_MULTIAMMOCRATE },
		{ "shield", OBJTYPE_SHIELD },
		{ "tag", OBJTYPE_TAG },
		{ "objective_begin", OBJTYPE_BEGINOBJECTIVE },
		{ "objective_end", OBJTYPE_ENDOBJECTIVE },
		{ "objective_destroy_object", OBJECTIVETYPE_DESTROYOBJ },
		{ "objective_complete_flags", OBJECTIVETYPE_COMPFLAGS },
		{ "objective_fail_flags", OBJECTIVETYPE_FAILFLAGS },
		{ "objective_collect_object", OBJECTIVETYPE_COLLECTOBJ },
		{ "objective_throw_object", OBJECTIVETYPE_THROWOBJ },
		{ "objective_holograph", OBJECTIVETYPE_HOLOGRAPH },
		{ "objective_marker", OBJECTIVETYPE_1F },
		{ "objective_enter_room", OBJECTIVETYPE_ENTERROOM },
		{ "objective_throw_in_room", OBJECTIVETYPE_THROWINROOM },
		{ "objective_marker_22", OBJTYPE_22 },
		{ "briefing", OBJTYPE_BRIEFING },
		{ "gas_bottle", OBJTYPE_GASBOTTLE },
		{ "rename_object", OBJTYPE_RENAMEOBJ },
		{ "padlocked_door", OBJTYPE_PADLOCKEDDOOR },
		{ "truck", OBJTYPE_TRUCK },
		{ "heli", OBJTYPE_HELI },
		{ "tank", OBJTYPE_TANK },
		{ "camera_position", OBJTYPE_CAMERAPOS },
		{ "glass", OBJTYPE_GLASS },
		{ "safe", OBJTYPE_SAFE },
		{ "safe_item", OBJTYPE_SAFEITEM },
		{ "tinted_glass", OBJTYPE_TINTEDGLASS },
		{ "lift", OBJTYPE_LIFT },
		{ "conditional_scenery", OBJTYPE_CONDITIONALSCENERY },
		{ "blocked_path", OBJTYPE_BLOCKEDPATH },
		{ "hoverbike", OBJTYPE_HOVERBIKE },
		{ "hover_prop", OBJTYPE_HOVERPROP },
		{ "fan", OBJTYPE_FAN },
		{ "hover_car", OBJTYPE_HOVERCAR },
		{ "pad_effect", OBJTYPE_PADEFFECT },
		{ "chopper", OBJTYPE_CHOPPER },
		{ "mine", OBJTYPE_MINE },
		{ "escalator_step", OBJTYPE_ESCASTEP },
	};

	if (!kind || !out_type) {
		return 0;
	}

	for (u32 i = 0; i < ARRAYCOUNT(maps); i++) {
		if (strcmp(kind, maps[i].kind) == 0) {
			*out_type = maps[i].type;
			return 1;
		}
	}

	return 0;
}

static s32 s_setupObjTypeHasDefaultBase(u8 type)
{
	switch (type) {
	case OBJTYPE_DOOR:
	case OBJTYPE_BASIC:
	case OBJTYPE_KEY:
	case OBJTYPE_ALARM:
	case OBJTYPE_CCTV:
	case OBJTYPE_AMMOCRATE:
	case OBJTYPE_WEAPON:
	case OBJTYPE_SINGLEMONITOR:
	case OBJTYPE_MULTIMONITOR:
	case OBJTYPE_HANGINGMONITORS:
	case OBJTYPE_AUTOGUN:
	case OBJTYPE_DEBRIS:
	case OBJTYPE_HAT:
	case OBJTYPE_MULTIAMMOCRATE:
	case OBJTYPE_SHIELD:
	case OBJTYPE_GASBOTTLE:
	case OBJTYPE_TRUCK:
	case OBJTYPE_HELI:
	case OBJTYPE_GLASS:
	case OBJTYPE_SAFE:
	case OBJTYPE_TINTEDGLASS:
	case OBJTYPE_LIFT:
	case OBJTYPE_HOVERBIKE:
	case OBJTYPE_HOVERPROP:
	case OBJTYPE_FAN:
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER:
	case OBJTYPE_MINE:
	case OBJTYPE_ESCASTEP:
		return 1;
	default:
		return 0;
	}
}

static u32 s_setupCommandLengthBytesForType(u8 type)
{
	switch (type) {
	case OBJTYPE_CHR:                return (u32)sizeof(struct packedchr);
	case OBJTYPE_DOOR:               return (u32)sizeof(struct doorobj);
	case OBJTYPE_DOORSCALE:          return (u32)sizeof(struct doorscaleobj);
	case OBJTYPE_BASIC:              return (u32)sizeof(struct defaultobj);
	case OBJTYPE_DEBRIS:             return (u32)sizeof(struct debrisobj);
	case OBJTYPE_GLASS:              return (u32)sizeof(struct glassobj);
	case OBJTYPE_TINTEDGLASS:        return (u32)sizeof(struct tintedglassobj);
	case OBJTYPE_SAFE:               return (u32)sizeof(struct safeobj);
	case OBJTYPE_GASBOTTLE:          return (u32)sizeof(struct gasbottleobj);
	case OBJTYPE_KEY:                return (u32)sizeof(struct keyobj);
	case OBJTYPE_ALARM:              return (u32)sizeof(struct alarmobj);
	case OBJTYPE_CCTV:               return (u32)sizeof(struct cctvobj);
	case OBJTYPE_AMMOCRATE:          return (u32)sizeof(struct ammocrateobj);
	case OBJTYPE_WEAPON:             return (u32)sizeof(struct weaponobj);
	case OBJTYPE_SINGLEMONITOR:      return (u32)sizeof(struct singlemonitorobj);
	case OBJTYPE_MULTIMONITOR:       return (u32)sizeof(struct multimonitorobj);
	case OBJTYPE_HANGINGMONITORS:    return (u32)sizeof(struct hangingmonitorsobj);
	case OBJTYPE_AUTOGUN:            return (u32)sizeof(struct autogunobj);
	case OBJTYPE_LINKGUNS:           return (u32)sizeof(struct linkgunsobj);
	case OBJTYPE_HAT:                return (u32)sizeof(struct hatobj);
	case OBJTYPE_GRENADEPROB:        return (u32)sizeof(struct grenadeprobobj);
	case OBJTYPE_LINKLIFTDOOR:       return (u32)sizeof(struct linkliftdoorobj);
	case OBJTYPE_SAFEITEM:           return (u32)sizeof(struct safeitemobj);
	case OBJTYPE_MULTIAMMOCRATE:     return (u32)sizeof(struct multiammocrateobj);
	case OBJTYPE_SHIELD:             return (u32)sizeof(struct shieldobj);
	case OBJTYPE_TAG:                return (u32)sizeof(struct tag);
	case OBJTYPE_RENAMEOBJ:          return (u32)sizeof(struct textoverride);
	case OBJTYPE_BEGINOBJECTIVE:     return (u32)sizeof(struct objective);
	case OBJTYPE_ENDOBJECTIVE:       return (u32)sizeof(u32);
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:     return (u32)(sizeof(u32) * 2u);
	case OBJECTIVETYPE_HOLOGRAPH:    return (u32)sizeof(struct criteria_holograph);
	case OBJECTIVETYPE_1F:           return (u32)sizeof(u32);
	case OBJECTIVETYPE_ENTERROOM:    return (u32)sizeof(struct criteria_roomentered);
	case OBJECTIVETYPE_THROWINROOM:  return (u32)sizeof(struct criteria_throwinroom);
	case OBJTYPE_22:                 return (u32)sizeof(u32);
	case OBJTYPE_BRIEFING:           return (u32)sizeof(struct briefingobj);
	case OBJTYPE_PADLOCKEDDOOR:      return (u32)sizeof(struct padlockeddoorobj);
	case OBJTYPE_TRUCK:              return (u32)sizeof(struct truckobj);
	case OBJTYPE_HELI:               return (u32)sizeof(struct heliobj);
	case OBJTYPE_TANK:               return (u32)(32u * sizeof(u32));
	case OBJTYPE_CAMERAPOS:          return (u32)sizeof(struct cameraposobj);
	case OBJTYPE_LIFT:               return (u32)sizeof(struct liftobj);
	case OBJTYPE_CONDITIONALSCENERY: return (u32)sizeof(struct linksceneryobj);
	case OBJTYPE_BLOCKEDPATH:        return (u32)sizeof(struct blockedpathobj);
	case OBJTYPE_HOVERBIKE:          return (u32)sizeof(struct hoverbikeobj);
	case OBJTYPE_HOVERPROP:          return (u32)sizeof(struct hoverpropobj);
	case OBJTYPE_FAN:                return (u32)sizeof(struct fanobj);
	case OBJTYPE_HOVERCAR:           return (u32)sizeof(struct hovercarobj);
	case OBJTYPE_CHOPPER:            return (u32)sizeof(struct chopperobj);
	case OBJTYPE_PADEFFECT:          return (u32)sizeof(struct padeffectobj);
	case OBJTYPE_MINE:               return (u32)sizeof(struct weaponobj);
	case OBJTYPE_ESCASTEP:           return (u32)sizeof(struct escalatorobj);
	default:                         return 0;
	}
}

static void s_setupTableSetError(scenario_source_setup_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}

	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_setupFreeTable(scenario_source_setup_table_t *table)
{
	if (!table) {
		return;
	}
	for (s32 i = 0; i < table->count; i++) {
		if (table->records[i].bytes) {
			free(table->records[i].bytes);
		}
	}
	free(table->records);
	memset(table, 0, sizeof(*table));
}

static void s_aiTableSetError(scenario_source_ai_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_aiFreeTable(scenario_source_ai_table_t *table)
{
	if (!table) {
		return;
	}
	for (s32 i = 0; i < table->count; i++) {
		for (s32 j = 0; j < table->lists[i].count; j++) {
			free(table->lists[i].commands[j].operands);
		}
		free(table->lists[i].commands);
	}
	free(table->lists);
	memset(table, 0, sizeof(*table));
}

static void s_spawnTableSetError(scenario_source_spawn_table_t *table,
	const char *fmt, const char *a, const char *b)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "", b ? b : "");
}

static void s_spawnFreeTable(scenario_source_spawn_table_t *table)
{
	if (!table) {
		return;
	}
	free(table->rows);
	memset(table, 0, sizeof(*table));
}

static void s_setupInitRecordBytes(scenario_source_setup_record_t *record)
{
	if (!record || !record->bytes) {
		return;
	}

	memset(record->bytes, 0, record->len);
	if (record->type == OBJTYPE_CHR) {
		struct packedchr *chr = (struct packedchr *)record->bytes;
		chr->typenum = OBJTYPE_CHR;
	} else if (s_setupObjTypeHasDefaultBase(record->type)) {
		struct defaultobj *obj = (struct defaultobj *)record->bytes;
		obj->type = record->type;
	} else {
		u32 word = PD_BE32((u32)record->type);
		memcpy(record->bytes, &word, sizeof(word));
	}
}

static scenario_source_setup_record_t *s_setupFindOrAddRecord(
	scenario_source_setup_table_t *table, const char *record_id,
	const char *kind, s32 default_order)
{
	u8 type;
	u32 len;
	scenario_source_setup_record_t *record;

	if (!table || !record_id || !record_id[0] || !kind || !kind[0]) {
		return NULL;
	}

	for (s32 i = 0; i < table->count; i++) {
		if (strcmp(table->records[i].id, record_id) == 0) {
			return &table->records[i];
		}
	}

	if (!s_setupKindToObjType(kind, &type)) {
		s_setupTableSetError(table,
			"unknown setup record kind '%s' for '%s'", kind, record_id);
		return NULL;
	}

	len = s_setupCommandLengthBytesForType(type);
	if (len == 0) {
		s_setupTableSetError(table,
			"unsupported setup record kind '%s' for '%s'", kind, record_id);
		return NULL;
	}

	if (table->count >= table->capacity) {
		s32 new_capacity = table->capacity ? table->capacity * 2 : 32;
		scenario_source_setup_record_t *new_records =
			(scenario_source_setup_record_t *)realloc(table->records,
				(size_t)new_capacity * sizeof(*new_records));
		if (!new_records) {
			s_setupTableSetError(table,
				"setup record allocation failed for '%s'", record_id, "");
			return NULL;
		}
		memset(new_records + table->capacity, 0,
			(size_t)(new_capacity - table->capacity) * sizeof(*new_records));
		table->records = new_records;
		table->capacity = new_capacity;
	}

	record = &table->records[table->count++];
	memset(record, 0, sizeof(*record));
	strncpy(record->id, record_id, sizeof(record->id) - 1);
	strncpy(record->kind, kind, sizeof(record->kind) - 1);
	record->type = type;
	record->order = default_order;
	record->len = len;
	record->bytes = (u8 *)malloc(len);
	if (!record->bytes) {
		s_setupTableSetError(table,
			"setup command allocation failed for '%s'", record_id, "");
		return NULL;
	}
	s_setupInitRecordBytes(record);
	return record;
}

static s32 s_setupWriteRaw(scenario_source_setup_record_t *record,
	u32 offset, const void *src, u32 size)
{
	if (!record || !record->bytes || !src || offset > record->len
			|| size > record->len - offset) {
		return 0;
	}
	memcpy(record->bytes + offset, src, size);
	return 1;
}

static s32 s_setupWriteS32(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupWriteU32(scenario_source_setup_record_t *record,
	u32 offset, u32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupWriteS16(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	s16 v = (s16)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU16(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	u16 v = (u16)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU8(scenario_source_setup_record_t *record,
	u32 offset, s32 value);
static s32 s_setupWriteS8(scenario_source_setup_record_t *record,
	u32 offset, s32 value);
static s32 s_setupWriteF32(scenario_source_setup_record_t *record,
	u32 offset, f32 value);

static s32 s_setupWriteRecordRefS16(scenario_source_setup_record_t *record,
	u32 offset, const char *ref_record_id)
{
	s32 ivalue;

	if (!ref_record_id || !ref_record_id[0]) {
		return s_setupWriteS16(record, offset, 0);
	}
	if (!s_setupRecordRefOffset(record, ref_record_id, &ivalue)) {
		return 0;
	}
	return s_setupWriteS16(record, offset, ivalue);
}

static s32 s_setupWriteRecordRefS32(scenario_source_setup_record_t *record,
	u32 offset, const char *ref_record_id)
{
	s32 ivalue;

	if (!ref_record_id || !ref_record_id[0]) {
		return s_setupWriteS32(record, offset, 0);
	}
	if (!s_setupRecordRefOffset(record, ref_record_id, &ivalue)) {
		return 0;
	}
	return s_setupWriteS32(record, offset, ivalue);
}

static const char *s_setupFieldAfterPrefix(const char *field,
	const char *prefix)
{
	size_t len;

	if (!field || !prefix) {
		return NULL;
	}

	len = strlen(prefix);
	if (strncmp(field, prefix, len) != 0 || field[len] != '.') {
		return NULL;
	}

	return field + len + 1;
}

static s32 s_setupApplyTvScreenField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *member, const char *value)
{
	s32 ivalue;
	f32 fvalue;

	if (!record || !member || !value) {
		return 0;
	}

#define TV_U16(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU16(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_S16(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteS16(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_U8(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU8(record, base_offset + (u32)offsetof(struct tvscreen, name), ivalue); \
	}
#define TV_F32(name) \
	if (strcmp(member, #name) == 0 && s_parseFloat(value, &fvalue)) { \
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct tvscreen, name), fvalue); \
	}

	TV_U16(offset);
	TV_S16(pause60);
	TV_F32(rot);
	TV_F32(xscale);
	TV_F32(xscalefrac);
	TV_F32(xscaleinc);
	TV_F32(xscaleold);
	TV_F32(xscalenew);
	TV_F32(yscale);
	TV_F32(yscalefrac);
	TV_F32(yscaleinc);
	TV_F32(yscaleold);
	TV_F32(yscalenew);
	TV_F32(xmid);
	TV_F32(xmidfrac);
	TV_F32(xmidinc);
	TV_F32(xmidold);
	TV_F32(xmidnew);
	TV_F32(ymid);
	TV_F32(ymidfrac);
	TV_F32(ymidinc);
	TV_F32(ymidold);
	TV_F32(ymidnew);
	TV_U8(red);
	TV_U8(redold);
	TV_U8(rednew);
	TV_U8(green);
	TV_U8(greenold);
	TV_U8(greennew);
	TV_U8(blue);
	TV_U8(blueold);
	TV_U8(bluenew);
	TV_U8(alpha);
	TV_U8(alphaold);
	TV_U8(alphanew);
	TV_F32(colfrac);
	TV_F32(colinc);

#undef TV_U16
#undef TV_S16
#undef TV_U8
#undef TV_F32
	return 0;
}

static s32 s_setupApplyCoordField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	const char *member = s_setupFieldAfterPrefix(field, prefix);
	f32 fvalue;

	if (!member) {
		return -1;
	}
	if (!s_parseFloat(value, &fvalue)) {
		return 0;
	}
	if (strcmp(member, "x") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, x), fvalue);
	}
	if (strcmp(member, "y") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, y), fvalue);
	}
	if (strcmp(member, "z") == 0) {
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct coord, z), fvalue);
	}
	return 0;
}

static s32 s_setupApplyF32ArrayField(scenario_source_setup_record_t *record,
	u32 base_offset, u32 count, const char *prefix, const char *field,
	const char *value)
{
	u32 index;
	size_t prefix_len;
	f32 fvalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u]", &index) != 1) {
		return -1;
	}
	if (index >= count || !s_parseFloat(value, &fvalue)) {
		return 0;
	}
	return s_setupWriteF32(record, base_offset + index * (u32)sizeof(f32), fvalue);
}

static s32 s_setupApplyU8ArrayField(scenario_source_setup_record_t *record,
	u32 base_offset, u32 count, const char *prefix, const char *field,
	const char *value)
{
	u32 index;
	size_t prefix_len;
	s32 ivalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u]", &index) != 1) {
		return -1;
	}
	if (index >= count || !s_parseInteger(value, &ivalue)) {
		return 0;
	}
	return s_setupWriteU8(record, base_offset + index, ivalue);
}

static s32 s_setupApplyF32Matrix3Field(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	u32 r;
	u32 c;
	size_t prefix_len;
	f32 fvalue;

	if (!prefix || !field) {
		return -1;
	}
	prefix_len = strlen(prefix);
	if (strncmp(field, prefix, prefix_len) != 0 ||
			sscanf(field + prefix_len, "[%u][%u]", &r, &c) != 2) {
		return -1;
	}
	if (r >= 3 || c >= 3 || !s_parseFloat(value, &fvalue)) {
		return 0;
	}
	return s_setupWriteF32(record,
		base_offset + ((r * 3u + c) * (u32)sizeof(f32)), fvalue);
}

static s32 s_setupApplyHoverField(scenario_source_setup_record_t *record,
	u32 base_offset, const char *prefix, const char *field,
	const char *value)
{
	const char *member = s_setupFieldAfterPrefix(field, prefix);
	s32 ivalue;
	f32 fvalue;

	if (!member) {
		return -1;
	}

#define HOV_U8(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteU8(record, base_offset + (u32)offsetof(struct hov, name), ivalue); \
	}
#define HOV_S32(name) \
	if (strcmp(member, #name) == 0 && s_parseInteger(value, &ivalue)) { \
		return s_setupWriteS32(record, base_offset + (u32)offsetof(struct hov, name), ivalue); \
	}
#define HOV_F32(name) \
	if (strcmp(member, #name) == 0 && s_parseFloat(value, &fvalue)) { \
		return s_setupWriteF32(record, base_offset + (u32)offsetof(struct hov, name), fvalue); \
	}
	HOV_U8(type);
	HOV_U8(flags);
	HOV_F32(bobycur);
	HOV_F32(bobytarget);
	HOV_F32(bobyspeed);
	HOV_F32(yrot);
	HOV_F32(bobpitchcur);
	HOV_F32(bobpitchtarget);
	HOV_F32(bobpitchspeed);
	HOV_F32(bobrollcur);
	HOV_F32(bobrolltarget);
	HOV_F32(bobrollspeed);
	HOV_F32(groundpitch);
	HOV_F32(y);
	HOV_F32(ground);
	HOV_S32(prevframe60);
	HOV_S32(prevgroundframe60);
#undef HOV_U8
#undef HOV_S32
#undef HOV_F32
	return 0;
}

static s32 s_setupWriteS8(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	s8 v = (s8)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteU8(scenario_source_setup_record_t *record,
	u32 offset, s32 value)
{
	u8 v = (u8)value;
	return s_setupWriteRaw(record, offset, &v, sizeof(v));
}

static s32 s_setupWriteF32(scenario_source_setup_record_t *record,
	u32 offset, f32 value)
{
	return s_setupWriteRaw(record, offset, &value, sizeof(value));
}

static s32 s_setupResolveModelnum(const char *catalog_id, s32 *out)
{
	catalog_model_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveModel(catalog_id, &result)) {
		return 0;
	}

	*out = result.modelnum;
	return 1;
}

static s32 s_setupResolveWeaponnum(const char *catalog_id, s32 *out)
{
	catalog_weapon_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveWeapon(catalog_id, &result)) {
		return 0;
	}

	*out = result.weapon_num;
	return 1;
}

static s32 s_setupResolveWeaponSelector(const char *value, s32 *out)
{
	s32 slot;

	if (!value || !out) {
		return 0;
	}

	if (strcmp(value, "none") == 0) {
		*out = WEAPON_NONE;
		return 1;
	}

	if (sscanf(value, "mp_location_%d", &slot) == 1 &&
			slot >= 0 && slot <= 15) {
		*out = WEAPON_MPLOCATION00 + slot;
		return 1;
	}

	return 0;
}

static s32 s_setupResolveBodyNum(const char *catalog_id, s32 *out)
{
	catalog_body_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveBody(catalog_id, &result) || !result.entry) {
		return 0;
	}

	*out = result.entry->runtime_index;
	return 1;
}

static s32 s_setupResolveHeadNum(const char *catalog_id, s32 *out)
{
	catalog_head_result_t result;

	if (!catalog_id || !catalog_id[0] || !out) {
		return 0;
	}

	if (!catalogResolveHead(catalog_id, &result) || !result.entry) {
		return 0;
	}

	*out = result.entry->runtime_index;
	return 1;
}

static scenario_source_ai_list_t *s_aiFindOrAddList(
	scenario_source_ai_table_t *table, const char *ref, s32 id)
{
	if (!table) {
		return NULL;
	}
	if (!ref || !ref[0]) {
		s_aiTableSetError(table, "missing AI list ref '%s'", "", "");
		return NULL;
	}
	for (s32 i = 0; i < table->count; i++) {
		if (strcmp(table->lists[i].ref, ref) == 0) {
			if (table->lists[i].id != id) {
				s_aiTableSetError(table,
					"AI list ref '%s' changed id", ref, "");
				return NULL;
			}
			return &table->lists[i];
		}
	}
	if (table->count >= table->capacity) {
		s32 new_capacity = table->capacity ? table->capacity * 2 : 16;
		scenario_source_ai_list_t *new_lists =
			(scenario_source_ai_list_t *)realloc(table->lists,
				(size_t)new_capacity * sizeof(*table->lists));
		if (!new_lists) {
			s_aiTableSetError(table, "out of memory adding AI list '%s'",
				"", "");
			return NULL;
		}
		memset(new_lists + table->capacity, 0,
			(size_t)(new_capacity - table->capacity) * sizeof(*new_lists));
		table->lists = new_lists;
		table->capacity = new_capacity;
	}
	scenario_source_ai_list_t *list = &table->lists[table->count++];
	memset(list, 0, sizeof(*list));
	snprintf(list->ref, sizeof(list->ref), "%s", ref);
	list->id = id;
	return list;
}

static s32 s_aiParseOperands(char *value, u8 **out_bytes, u32 *out_count)
{
	u8 *bytes;
	u32 count = 0;
	u32 capacity = 0;

	if (out_bytes) *out_bytes = NULL;
	if (out_count) *out_count = 0;
	if (!value || !out_bytes || !out_count || !value[0]) {
		return 1;
	}

	char *cursor = value;
	while (cursor && *cursor) {
		char *token = cursor;
		char *comma = strchr(cursor, ',');
		u32 parsed;
		if (comma) {
			*comma = '\0';
			cursor = comma + 1;
		} else {
			cursor = NULL;
		}
		while (*token && isspace((unsigned char)*token)) token++;
		if (!*token) {
			continue;
		}
		if (!s_parseU32Value(token, &parsed) || parsed > 0xffu) {
			free(*out_bytes);
			*out_bytes = NULL;
			*out_count = 0;
			return 0;
		}
		if (count >= capacity) {
			u32 new_capacity = capacity ? capacity * 2u : 16u;
			bytes = (u8 *)realloc(*out_bytes, new_capacity);
			if (!bytes) {
				free(*out_bytes);
				*out_bytes = NULL;
				*out_count = 0;
				return 0;
			}
			*out_bytes = bytes;
			capacity = new_capacity;
		}
		(*out_bytes)[count++] = (u8)parsed;
	}
	*out_count = count;
	return 1;
}

static s32 s_aiApplyCatalogOperands(scenario_source_ai_table_t *table,
	scenario_source_ai_command_t *cmd, const char *model_id,
	const char *weapon_id, const char *body_id, const char *head_id)
{
	s32 value;

	if (!cmd) {
		return 0;
	}

	if ((cmd->opcode == AICMD_DROPITEM || cmd->opcode == AICMD_EQUIPHAT) &&
			model_id && model_id[0]) {
		if (cmd->operand_count < 2 ||
				!s_setupResolveModelnum(model_id, &value)) {
			s_aiTableSetError(table, "bad AI model catalog id '%s'",
				model_id, "");
			return 0;
		}
		cmd->operands[0] = (u8)((value >> 8) & 0xff);
		cmd->operands[1] = (u8)(value & 0xff);
	}

	if (cmd->opcode == AICMD_EQUIPWEAPON) {
		if (model_id && model_id[0]) {
			if (cmd->operand_count < 2 ||
					!s_setupResolveModelnum(model_id, &value)) {
				s_aiTableSetError(table, "bad AI weapon model catalog id '%s'",
					model_id, "");
				return 0;
			}
			cmd->operands[0] = (u8)((value >> 8) & 0xff);
			cmd->operands[1] = (u8)(value & 0xff);
		}
		if (weapon_id && weapon_id[0]) {
			if (cmd->operand_count < 3 ||
					!s_setupResolveWeaponnum(weapon_id, &value)) {
				s_aiTableSetError(table, "bad AI weapon catalog id '%s'",
					weapon_id, "");
				return 0;
			}
			cmd->operands[2] = (u8)value;
		}
	}

	if ((cmd->opcode == AICMD_SPAWNCHRATPAD ||
			cmd->opcode == AICMD_SPAWNCHRATCHR)) {
		if (body_id && body_id[0]) {
			if (cmd->operand_count < 1 ||
					!s_setupResolveBodyNum(body_id, &value)) {
				s_aiTableSetError(table, "bad AI body catalog id '%s'",
					body_id, "");
				return 0;
			}
			cmd->operands[0] = (u8)value;
		}
		if (head_id && head_id[0]) {
			if (cmd->operand_count < 2 ||
					!s_setupResolveHeadNum(head_id, &value)) {
				s_aiTableSetError(table, "bad AI head catalog id '%s'",
					head_id, "");
				return 0;
			}
			cmd->operands[1] = (u8)value;
		}
	}

	return 1;
}

static s32 s_aiAppendCommand(scenario_source_ai_table_t *table,
	scenario_source_ai_list_t *list, scenario_source_ai_command_t *cmd)
{
	if (!table || !list || !cmd) {
		return 0;
	}
	if (list->count >= list->capacity) {
		s32 new_capacity = list->capacity ? list->capacity * 2 : 16;
		scenario_source_ai_command_t *commands =
			(scenario_source_ai_command_t *)realloc(list->commands,
				(size_t)new_capacity * sizeof(*list->commands));
		if (!commands) {
			s_aiTableSetError(table, "out of memory adding AI command '%s'",
				"", "");
			return 0;
		}
		memset(commands + list->capacity, 0,
			(size_t)(new_capacity - list->capacity) * sizeof(*commands));
		list->commands = commands;
		list->capacity = new_capacity;
	}
	list->commands[list->count++] = *cmd;
	list->byte_count += 2u + cmd->operand_count;
	table->byte_count += 2u + cmd->operand_count;
	memset(cmd, 0, sizeof(*cmd));
	return 1;
}

static s32 s_loadAiListSourceRows(char *text,
	scenario_source_ai_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *ailist_ref = s_nextField(&line_cursor);
		char *list_id = s_nextField(&line_cursor);
		char *graph_node = s_nextField(&line_cursor);
		char *command_index = s_nextField(&line_cursor);
		char *offset = s_nextField(&line_cursor);
		char *opcode = s_nextField(&line_cursor);
		char *opcode_name = s_nextField(&line_cursor);
		char *operands = s_nextField(&line_cursor);
		char *model_id = s_nextField(&line_cursor);
		char *weapon_id = s_nextField(&line_cursor);
		char *body_id = s_nextField(&line_cursor);
		char *head_id = s_nextField(&line_cursor);
		u32 parsed;
		u32 parsed_opcode;
		scenario_source_ai_list_t *list;
		scenario_source_ai_command_t cmd;

		(void)graph_node;
		(void)command_index;
		(void)offset;
		(void)opcode_name;

		if (!ailist_ref || !list_id || !opcode) {
			continue;
		}
		if (strcmp(ailist_ref, "ailist_ref") == 0) {
			continue;
		}
		if (!s_parseU32Value(list_id, &parsed) ||
				!s_parseU32Value(opcode, &parsed_opcode) ||
				parsed_opcode > 0xffffu) {
			s_aiTableSetError(table, "bad AI row '%s'", ailist_ref, "");
			return 0;
		}

		list = s_aiFindOrAddList(table, ailist_ref, (s32)parsed);
		if (!list) {
			return 0;
		}

		memset(&cmd, 0, sizeof(cmd));
		cmd.opcode = (u16)parsed_opcode;
		if (!s_aiParseOperands(operands, &cmd.operands,
				&cmd.operand_count) ||
				!s_aiApplyCatalogOperands(table, &cmd, model_id,
				weapon_id, body_id, head_id) ||
				!s_aiAppendCommand(table, list, &cmd)) {
			free(cmd.operands);
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_parseSpawnTeam(const char *value)
{
	if (!value || !value[0] || strcmp(value, "any") == 0 ||
			strcmp(value, "default") == 0) {
		return 0;
	}

	return s_parseIndexedRef(value);
}

static s32 s_spawnAppendRow(scenario_source_spawn_table_t *table,
	s32 padnum, s32 team)
{
	scenario_source_spawn_row_t *rows;
	s32 new_capacity;

	if (!table || padnum < 0 || padnum > 0x7fff) {
		return 0;
	}
	if (table->count >= table->capacity) {
		new_capacity = table->capacity ? table->capacity * 2 : 32;
		rows = (scenario_source_spawn_row_t *)realloc(table->rows,
			(size_t)new_capacity * sizeof(*table->rows));
		if (!rows) {
			s_spawnTableSetError(table,
				"out of memory adding spawn '%s'", "", "");
			return 0;
		}
		table->rows = rows;
		table->capacity = new_capacity;
	}
	table->rows[table->count].padnum = padnum;
	table->rows[table->count].team = team >= 0 ? team : 0;
	table->count++;
	return 1;
}

static s32 s_loadSpawnSourceRows(char *text,
	scenario_source_spawn_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *spawn_id = s_nextField(&line_cursor);
		char *pad_ref = s_nextField(&line_cursor);
		char *room_ref = s_nextField(&line_cursor);
		char *team = s_nextField(&line_cursor);
		char *profile = s_nextField(&line_cursor);
		s32 padnum;
		s32 teamnum;

		(void)room_ref;
		(void)profile;

		if (!spawn_id || !pad_ref || !team) {
			continue;
		}
		if (strcmp(spawn_id, "spawn_id") == 0) {
			continue;
		}

		padnum = s_parseIndexedRef(pad_ref);
		teamnum = s_parseSpawnTeam(team);
		if (padnum < 0 || padnum > 0x7fff || teamnum < 0) {
			s_spawnTableSetError(table, "bad spawn row '%s'",
				spawn_id, "");
			return 0;
		}
		if (!s_spawnAppendRow(table, padnum, teamnum)) {
			s_spawnTableSetError(table, "bad spawn row '%s'",
				spawn_id, "");
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_setupApplyDefaultField(scenario_source_setup_record_t *record,
	const char *field, const char *type, const char *value,
	const char *catalog_id)
{
	s32 ivalue;
	s32 applied;
	u32 uvalue;
	f32 fvalue;
	u32 r;
	u32 c;
	u32 index;

	if (!record || !field) {
		return 0;
	}

	if (strcmp(field, "base.extra_scale") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct defaultobj, extrascale), ivalue);
	}
	if (strcmp(field, "base.hidden2") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct defaultobj, hidden2), ivalue);
	}
	if (strcmp(field, "base.model") == 0) {
		if (!catalog_id || !catalog_id[0]) {
			return 1;
		}
		if (!s_setupResolveModelnum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, modelnum), ivalue);
	}
	if (strcmp(field, "base.pad") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, pad), ivalue);
	}
	if (strcmp(field, "base.flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags), uvalue);
	}
	if (strcmp(field, "base.flags2") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags2), uvalue);
	}
	if (strcmp(field, "base.flags3") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, flags3), uvalue);
	}
	if (sscanf(field, "base.rotation[%u][%u]", &r, &c) == 2 &&
			r < 3 && c < 3 && s_parseFloat(value, &fvalue)) {
		return s_setupWriteF32(record,
			(u32)offsetof(struct defaultobj, realrot)
				+ ((r * 3u + c) * (u32)sizeof(f32)),
			fvalue);
	}
	if (strcmp(field, "base.hidden") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct defaultobj, hidden), uvalue);
	}
	if (strcmp(field, "base.damage") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, damage), ivalue);
	}
	if (strcmp(field, "base.max_damage") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct defaultobj, maxdamage), ivalue);
	}
	if (sscanf(field, "base.shade_color[%u]", &index) == 1 &&
			index < 4 && s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record,
			(u32)offsetof(struct defaultobj, shadecol) + index, ivalue);
	}
	if (sscanf(field, "base.next_color[%u]", &index) == 1 &&
			index < 4 && s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record,
			(u32)offsetof(struct defaultobj, nextcol) + index, ivalue);
	}
	if (strcmp(field, "base.floor_color") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct defaultobj, floorcol), ivalue);
	}
	if (strcmp(field, "base.geo_count") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS8(record, (u32)offsetof(struct defaultobj, geocount), ivalue);
	}

	(void)type;
	return 0;
}

static s32 s_setupApplyCharacterField(scenario_source_setup_record_t *record,
	const char *field, const char *type, const char *value,
	const char *catalog_id)
{
	s32 ivalue;
	u32 uvalue;

	if (strcmp(field, "character.index") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chrindex), ivalue);
	}
	if (strcmp(field, "character.kind") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS8(record, (u32)offsetof(struct packedchr, typenum), ivalue);
	}
	if (strcmp(field, "character.spawn_flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, spawnflags), uvalue);
	}
	if (strcmp(field, "character.slot") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chrnum), ivalue);
	}
	if (strcmp(field, "character.pad") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, padnum), ivalue);
	}
	if (strcmp(field, "character.body") == 0) {
		if (!s_setupResolveBodyNum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, bodynum), ivalue);
	}
	if (strcmp(field, "character.head") == 0) {
		if (type && strcmp(type, "head_selector") == 0 &&
				value && strcmp(value, "random") == 0) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum),
				HEAD_RANDOM);
		}
		if (type && strcmp(type, "head_selector") == 0 &&
				value && strcmp(value, "embedded") == 0) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), 0);
		}
		if (!catalog_id || !catalog_id[0]) {
			return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), -1);
		}
		if (!s_setupResolveHeadNum(catalog_id, &ivalue)) {
			return 0;
		}
		return s_setupWriteS8(record, (u32)offsetof(struct packedchr, headnum), ivalue);
	}
	if (strcmp(field, "character.ai_list") == 0) {
		ivalue = s_parseIndexedRef(value);
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, ailistnum), ivalue);
	}
	if (strcmp(field, "character.pad_preset") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, padpreset), ivalue);
	}
	if (strcmp(field, "character.character_preset") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, chrpreset), ivalue);
	}
	if (strcmp(field, "character.hearing_scale") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, hearscale), ivalue);
	}
	if (strcmp(field, "character.view_distance") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU16(record, (u32)offsetof(struct packedchr, viewdist), ivalue);
	}
	if (strcmp(field, "character.flags") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, flags), uvalue);
	}
	if (strcmp(field, "character.flags2") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, flags2), uvalue);
	}
	if (strcmp(field, "character.team") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, team), ivalue);
	}
	if (strcmp(field, "character.squadron") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, squadron), ivalue);
	}
	if (strcmp(field, "character.chair") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteS16(record, (u32)offsetof(struct packedchr, chair), ivalue);
	}
	if (strcmp(field, "character.conversation_talk") == 0 &&
			s_parseU32Value(value, &uvalue)) {
		return s_setupWriteU32(record, (u32)offsetof(struct packedchr, convtalk), uvalue);
	}
	if (strcmp(field, "character.attitude") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, tude), ivalue);
	}
	if (strcmp(field, "character.natural_animation") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, naturalanim), ivalue);
	}
	if (strcmp(field, "character.visible_yaw_angle") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, yvisang), ivalue);
	}
	if (strcmp(field, "character.team_scan_distance") == 0 &&
			s_parseInteger(value, &ivalue)) {
		return s_setupWriteU8(record, (u32)offsetof(struct packedchr, teamscandist), ivalue);
	}

	return 0;
}

static s32 s_setupApplyKnownField(scenario_source_setup_table_t *table,
	scenario_source_setup_record_t *record, const char *field,
	const char *type, const char *value, const char *catalog_id,
	const char *ref_record_id)
{
	s32 ivalue;
	s32 applied;
	u32 uvalue;
	f32 fvalue;

	if (!record || !field || !field[0]) {
		return 1;
	}

	if (strcmp(field, "command.order") == 0) {
		if (s_parseInteger(value, &ivalue)) {
			record->order = ivalue;
			return 1;
		}
		return 0;
	}

	if (s_setupObjTypeHasDefaultBase(record->type)
			&& strncmp(field, "base.", 5) == 0) {
		return s_setupApplyDefaultField(record, field, type, value,
			catalog_id);
	}

	if (record->type == OBJTYPE_CHR
			&& strncmp(field, "character.", 10) == 0) {
		return s_setupApplyCharacterField(record, field, type, value,
			catalog_id);
	}

	switch (record->type) {
	case OBJTYPE_DOOR:
#define DOOR_F32(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseFloat(value, &fvalue)) { \
			return s_setupWriteF32(record, (u32)offsetof(struct doorobj, name), fvalue); \
		}
#define DOOR_U8(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteU8(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S8(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS8(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S16(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS16(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_U16(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteU16(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
#define DOOR_S32(name) \
		if (strcmp(field, "door." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS32(record, (u32)offsetof(struct doorobj, name), ivalue); \
		}
		DOOR_F32(maxfrac);
		DOOR_F32(perimfrac);
		DOOR_F32(accel);
		DOOR_F32(decel);
		DOOR_F32(maxspeed);
		DOOR_U16(doorflags);
		DOOR_U16(doortype);
		if (strcmp(field, "door.key_flags") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct doorobj, keyflags), uvalue);
		}
		DOOR_S32(autoclosetime);
		DOOR_F32(frac);
		DOOR_F32(fracspeed);
		DOOR_S8(mode);
		DOOR_S8(glasshits);
		DOOR_S16(fadealpha);
		DOOR_S16(xludist);
		DOOR_S16(opadist);
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct doorobj, startpos),
			"door.start_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32Matrix3Field(record,
			(u32)offsetof(struct doorobj, mtx98),
			"door.matrix", field, value);
		if (applied >= 0) {
			return applied;
		}
		DOOR_S32(lastopen60);
		DOOR_S16(portalnum);
		DOOR_S8(soundtype);
		DOOR_S8(fadetime60);
		DOOR_S32(lastcalc60);
		DOOR_U8(laserfade);
		applied = s_setupApplyU8ArrayField(record,
			(u32)offsetof(struct doorobj, shadeinfo1),
			4, "door.shade_info_player1", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyU8ArrayField(record,
			(u32)offsetof(struct doorobj, shadeinfo2),
			4, "door.shade_info_player2", field, value);
		if (applied >= 0) {
			return applied;
		}
		DOOR_U8(actual1);
		DOOR_U8(actual2);
		DOOR_U8(extra1);
		DOOR_U8(extra2);
#undef DOOR_F32
#undef DOOR_U8
#undef DOOR_S8
#undef DOOR_S16
#undef DOOR_U16
#undef DOOR_S32
		break;
	case OBJTYPE_DOORSCALE:
		if (strcmp(field, "door_scale.scale") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct doorscaleobj, scale), ivalue);
		}
		break;
	case OBJTYPE_KEY:
		if (strcmp(field, "key.flags") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct keyobj, keyflags), uvalue);
		}
		break;
	case OBJTYPE_CCTV:
		if (strcmp(field, "cctv.look_at_pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS16(record, (u32)offsetof(struct cctvobj, lookatpadnum), ivalue);
		}
		if (strcmp(field, "cctv.to_left") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct cctvobj, toleft), ivalue);
		}
		if (strcmp(field, "cctv.y_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yzero), fvalue);
		}
		if (strcmp(field, "cctv.y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yrot), fvalue);
		}
		if (strcmp(field, "cctv.y_left") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yleft), fvalue);
		}
		if (strcmp(field, "cctv.y_right") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yright), fvalue);
		}
		if (strcmp(field, "cctv.y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, yspeed), fvalue);
		}
		if (strcmp(field, "cctv.y_max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, ymaxspeed), fvalue);
		}
		if (strcmp(field, "cctv.see_bond_time60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct cctvobj, seebondtime60), ivalue);
		}
		if (strcmp(field, "cctv.max_distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, maxdist), fvalue);
		}
		if (strcmp(field, "cctv.x_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cctvobj, xzero), fvalue);
		}
		break;
	case OBJTYPE_AMMOCRATE:
		if (strcmp(field, "ammo_crate.ammo_kind") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct ammocrateobj, ammotype), ivalue);
		}
		break;
	case OBJTYPE_WEAPON:
	case OBJTYPE_MINE:
		if (strcmp(field, "pickup.weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, weaponnum),
					ivalue);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, weaponnum), ivalue);
		}
		if (strcmp(field, "pickup.unknown_5d") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, unk5d), ivalue);
		}
		if (strcmp(field, "pickup.unknown_5e") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, unk5e), ivalue);
		}
		if (strcmp(field, "pickup.fire_mode") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct weaponobj, gunfunc), ivalue);
		}
		if (strcmp(field, "pickup.fadeout_timer60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, fadeouttimer60), ivalue);
		}
		if (strcmp(field, "pickup.dual_weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0 &&
					value && strcmp(value, "none") == 0) {
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum),
					-1);
			}
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum),
					ivalue);
			}
			if (!catalog_id || !catalog_id[0]) {
				return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum), -1);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteS8(record, (u32)offsetof(struct weaponobj, dualweaponnum), ivalue);
		}
		if (strcmp(field, "pickup.team_or_timer240") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct weaponobj, team), ivalue);
		}
		break;
	case OBJTYPE_AUTOGUN:
#define AUTOGUN_F32(name) \
		if (strcmp(field, "autogun." #name) == 0 && s_parseFloat(value, &fvalue)) { \
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, name), fvalue); \
		}
#define AUTOGUN_S32(name) \
		if (strcmp(field, "autogun." #name) == 0 && s_parseInteger(value, &ivalue)) { \
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, name), ivalue); \
		}
		if (strcmp(field, "autogun.target_pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS16(record, (u32)offsetof(struct autogunobj, targetpad), ivalue);
		}
		if (strcmp(field, "autogun.firing") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct autogunobj, firing), ivalue);
		}
		if (strcmp(field, "autogun.fire_count") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, firecount), ivalue);
		}
		if (strcmp(field, "autogun.y_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, yzero), fvalue);
		}
		if (strcmp(field, "autogun.y_max_left") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, ymaxleft), fvalue);
		}
		if (strcmp(field, "autogun.y_max_right") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, ymaxright), fvalue);
		}
		if (strcmp(field, "autogun.y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, yrot), fvalue);
		}
		if (strcmp(field, "autogun.y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, yspeed), fvalue);
		}
		if (strcmp(field, "autogun.x_zero") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, xzero), fvalue);
		}
		if (strcmp(field, "autogun.x_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, xrot), fvalue);
		}
		if (strcmp(field, "autogun.x_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, xspeed), fvalue);
		}
		if (strcmp(field, "autogun.max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, maxspeed), fvalue);
		}
		AUTOGUN_F32(yzero);
		AUTOGUN_F32(ymaxleft);
		AUTOGUN_F32(ymaxright);
		AUTOGUN_F32(yrot);
		AUTOGUN_F32(yspeed);
		AUTOGUN_F32(xzero);
		AUTOGUN_F32(xrot);
		AUTOGUN_F32(xspeed);
		AUTOGUN_F32(maxspeed);
		if (strcmp(field, "autogun.aim_distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, aimdist), fvalue);
		}
		if (strcmp(field, "autogun.barrel_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, barrelspeed), fvalue);
		}
		if (strcmp(field, "autogun.barrel_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, barrelrot), fvalue);
		}
		if (strcmp(field, "autogun.last_see_bond60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, lastseebond60), ivalue);
		}
		if (strcmp(field, "autogun.last_aim_bond60") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, lastaimbond60), ivalue);
		}
		if (strcmp(field, "autogun.allow_sound_frame") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct autogunobj, allowsoundframe), ivalue);
		}
		if (strcmp(field, "autogun.shot_bond_sum") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct autogunobj, shotbondsum), fvalue);
		}
		if (strcmp(field, "autogun.target_team") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, targetteam), ivalue);
		}
		if (strcmp(field, "autogun.ammo_quantity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct autogunobj, ammoquantity), ivalue);
		}
		if (strcmp(field, "autogun.next_character_test") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct autogunobj, nextchrtest), ivalue);
		}
#undef AUTOGUN_F32
#undef AUTOGUN_S32
		break;
	case OBJTYPE_LINKGUNS:
		if (strcmp(field, "linked_guns.weapon_1") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct linkgunsobj, offset1),
				ref_record_id);
		}
		if (strcmp(field, "linked_guns.weapon_2") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct linkgunsobj, offset2),
				ref_record_id);
		}
		break;
	case OBJTYPE_GRENADEPROB:
		if (strcmp(field, "grenade_probability.character") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct grenadeprobobj, chrnum), ivalue);
		}
		if (strcmp(field, "grenade_probability.percent") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct grenadeprobobj, probability), ivalue);
		}
		break;
	case OBJTYPE_SINGLEMONITOR: {
		const char *member = s_setupFieldAfterPrefix(field, "monitor.screen");
		if (member) {
			return s_setupApplyTvScreenField(record,
				(u32)offsetof(struct singlemonitorobj, screen),
				member, value);
		}
		if (strcmp(field, "monitor.owner") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct singlemonitorobj, owneroffset),
				ref_record_id);
		}
		if (strcmp(field, "monitor.owner_part") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record,
				(u32)offsetof(struct singlemonitorobj, ownerpart), ivalue);
		}
		if (strcmp(field, "monitor.image") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record,
				(u32)offsetof(struct singlemonitorobj, imagenum), ivalue);
		}
		break;
	}
	case OBJTYPE_MULTIMONITOR: {
		u32 index;
		char member[64];

		if (sscanf(field, "monitor.screens[%u].%63s", &index, member) == 2
				&& index < 4) {
			return s_setupApplyTvScreenField(record,
				(u32)offsetof(struct multimonitorobj, screens)
					+ index * (u32)sizeof(struct tvscreen),
				member, value);
		}
		if (sscanf(field, "monitor.images[%u]", &index) == 1
				&& index < 4 && s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record,
				(u32)offsetof(struct multimonitorobj, imagenums) + index,
				ivalue);
		}
		break;
	}
	case OBJTYPE_LINKLIFTDOOR:
		if (strcmp(field, "lift_door_link.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linkliftdoorobj, door),
				ref_record_id);
		}
		if (strcmp(field, "lift_door_link.lift") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linkliftdoorobj, lift),
				ref_record_id);
		}
		if (strcmp(field, "lift_door_link.stop") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct linkliftdoorobj, stopnum), ivalue);
		}
		break;
	case OBJTYPE_MULTIAMMOCRATE: {
		u32 index;
		char member[32];
		u32 slot_base;

		if (sscanf(field, "multi_ammo_crate.slots[%u].%31s",
				&index, member) != 2 ||
				index >= ARRAYCOUNT(((struct multiammocrateobj *)0)->slots)) {
			break;
		}

		slot_base = (u32)offsetof(struct multiammocrateobj, slots)
			+ index * (u32)sizeof(struct multiammocrateslot);
		if (strcmp(member, "model") == 0) {
			if (!s_setupResolveModelnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteU16(record,
				slot_base + (u32)offsetof(struct multiammocrateslot, modelnum),
				ivalue);
		}
		if (strcmp(member, "quantity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record,
				slot_base + (u32)offsetof(struct multiammocrateslot, quantity),
				ivalue);
		}
		break;
	}
	case OBJTYPE_SHIELD:
		if (strcmp(field, "shield.initial_amount") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct shieldobj, initialamount), fvalue);
		}
		if (strcmp(field, "shield.amount") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct shieldobj, amount), fvalue);
		}
		if (strcmp(field, "shield.unknown_64") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct shieldobj, unk64), uvalue);
		}
		break;
	case OBJTYPE_TAG:
		if (strcmp(field, "tag.id") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct tag, tagnum), ivalue);
		}
		if (strcmp(field, "tag.target") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS16(record,
				(u32)offsetof(struct tag, cmdoffset), ref_record_id);
		}
		break;
	case OBJTYPE_BEGINOBJECTIVE:
		if (strcmp(field, "objective.index") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct objective, index), ivalue);
		}
		if (strcmp(field, "objective.text_token") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct objective, text), uvalue);
		}
		if (strcmp(field, "objective.flags") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU8(record, (u32)offsetof(struct objective, flags), ivalue);
		}
		if (strcmp(field, "objective.difficulty_mask") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct objective, difficulties), (s32)uvalue);
		}
		break;
	case OBJECTIVETYPE_DESTROYOBJ:
	case OBJECTIVETYPE_COLLECTOBJ:
	case OBJECTIVETYPE_THROWOBJ:
		if (strcmp(field, "objective_step.target_tag") == 0) {
			u32 be;
			ivalue = s_parseIndexedRef(value);
			if (ivalue < 0) {
				return 0;
			}
			be = PD_BE32((u32)ivalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		if (strcmp(field, "objective_step.argument") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		break;
	case OBJECTIVETYPE_COMPFLAGS:
	case OBJECTIVETYPE_FAILFLAGS:
		if (strcmp(field, "objective_step.stage_flag") == 0 &&
				s_parseStageFlagRef(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		if (strcmp(field, "objective_step.argument") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			u32 be = PD_BE32(uvalue);
			return s_setupWriteU32(record, sizeof(u32), be);
		}
		break;
	case OBJECTIVETYPE_HOLOGRAPH:
		if ((strcmp(field, "objective_holograph.target_tag") == 0 ||
				strcmp(field, "objective_holograph.object") == 0) &&
				((ivalue = s_parseIndexedRef(value)) >= 0 ||
					s_parseInteger(value, &ivalue))) {
			return s_setupWriteU32(record, (u32)offsetof(struct criteria_holograph, obj), (u32)ivalue);
		}
		if (strcmp(field, "objective_holograph.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct criteria_holograph, status), (u32)ivalue);
		}
		break;
	case OBJECTIVETYPE_ENTERROOM:
		if (strcmp(field, "objective_enter_room.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_roomentered, pad), ivalue);
		}
		if (strcmp(field, "objective_enter_room.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_roomentered, status), ivalue);
		}
		break;
	case OBJECTIVETYPE_THROWINROOM:
		if (strcmp(field, "objective_throw_in_room.match_value") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, unk04), ivalue);
		}
		if (strcmp(field, "objective_throw_in_room.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, pad), ivalue);
		}
		if (strcmp(field, "objective_throw_in_room.status") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct criteria_throwinroom, status), ivalue);
		}
		break;
	case OBJTYPE_BRIEFING:
		if (strcmp(field, "briefing.kind") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct briefingobj, type), (u32)ivalue);
		}
		if (strcmp(field, "briefing.text_token") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record, (u32)offsetof(struct briefingobj, text), uvalue);
		}
		break;
	case OBJTYPE_RENAMEOBJ:
		if (strcmp(field, "rename_object.target") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct textoverride, objoffset), ref_record_id);
		}
		if (strcmp(field, "rename_object.weapon") == 0) {
			if (type && strcmp(type, "weapon_selector") == 0) {
				if (!s_setupResolveWeaponSelector(value, &ivalue)) {
					return 0;
				}
				return s_setupWriteS32(record,
					(u32)offsetof(struct textoverride, weapon), ivalue);
			}
			if (!s_setupResolveWeaponnum(catalog_id, &ivalue)) {
				return 0;
			}
			return s_setupWriteS32(record,
				(u32)offsetof(struct textoverride, weapon), ivalue);
		}
		if (strcmp(field, "rename_object.obtain_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, obtaintext), uvalue);
		}
		if (strcmp(field, "rename_object.owner_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, ownertext), uvalue);
		}
		if (strcmp(field, "rename_object.inventory_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, inventorytext), uvalue);
		}
		if (strcmp(field, "rename_object.inventory2_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, inventory2text), uvalue);
		}
		if (strcmp(field, "rename_object.pickup_text") == 0 &&
				s_parseU32Value(value, &uvalue)) {
			return s_setupWriteU32(record,
				(u32)offsetof(struct textoverride, pickuptext), uvalue);
		}
		break;
	case OBJTYPE_TRUCK:
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct truckobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct truckobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.wheel_x_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, wheelxrot), fvalue);
		}
		if (strcmp(field, "vehicle.wheel_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, wheelyrot), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_rot60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, turnrot60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct truckobj, roty), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct truckobj, nextstep), ivalue);
		}
		break;
	case OBJTYPE_HELI:
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteU16(record, (u32)offsetof(struct heliobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct heliobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.rotor_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryrot), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeed), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeedaim), fvalue);
		}
		if (strcmp(field, "vehicle.rotor_y_speed_time") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, rotoryspeedtime), fvalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct heliobj, yrot), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct heliobj, nextstep), ivalue);
		}
		break;
	case OBJTYPE_HOVERCAR:
	case OBJTYPE_CHOPPER: {
		u32 common_base = 0;

		if (record->type == OBJTYPE_CHOPPER) {
			common_base = (u32)offsetof(struct chopperobj, base);
		} else {
			common_base = (u32)offsetof(struct hovercarobj, base);
		}

		(void)common_base;
		if (strcmp(field, "vehicle.ai_offset") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteU16(record, (u32)offsetof(struct chopperobj, aioffset), ivalue)
				: s_setupWriteU16(record, (u32)offsetof(struct hovercarobj, aioffset), ivalue);
		}
		if (strcmp(field, "vehicle.ai_return_list") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteS16(record, (u32)offsetof(struct chopperobj, aireturnlist), ivalue)
				: s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, aireturnlist), ivalue);
		}
		if (strcmp(field, "vehicle.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speed), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speed), fvalue);
		}
		if (strcmp(field, "vehicle.speed_aim") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speedaim), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speedaim), fvalue);
		}
		if (strcmp(field, "vehicle.speed_time60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, speedtime60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, speedtime60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_y_speed60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnyspeed60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnyspeed60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_x_speed60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnxspeed60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnxspeed60), fvalue);
		}
		if (strcmp(field, "vehicle.turn_rot60") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, turnrot60), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, turnrot60), fvalue);
		}
		if (strcmp(field, "vehicle.rot_y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, roty), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, roty), fvalue);
		}
		if (strcmp(field, "vehicle.rot_x") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, rotx), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, rotx), fvalue);
		}
		if (strcmp(field, "vehicle.rot_z") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteF32(record, (u32)offsetof(struct chopperobj, rotz), fvalue)
				: s_setupWriteF32(record, (u32)offsetof(struct hovercarobj, rotz), fvalue);
		}
		if (strcmp(field, "vehicle.next_step") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return record->type == OBJTYPE_CHOPPER
				? s_setupWriteS32(record, (u32)offsetof(struct chopperobj, nextstep), ivalue)
				: s_setupWriteS32(record, (u32)offsetof(struct hovercarobj, nextstep), ivalue);
		}
		if (record->type == OBJTYPE_HOVERCAR) {
			if (strcmp(field, "vehicle.status") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, status), ivalue);
			}
			if (strcmp(field, "vehicle.dead") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, dead), ivalue);
			}
			if (strcmp(field, "vehicle.dead_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, deadtimer60), ivalue);
			}
			if (strcmp(field, "vehicle.sparks_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct hovercarobj, sparkstimer60), ivalue);
			}
		} else {
			if (strcmp(field, "chopper.weapons_armed") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, weaponsarmed), ivalue);
			}
			if (strcmp(field, "chopper.on_target") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, ontarget), ivalue);
			}
			if (strcmp(field, "chopper.target") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS16(record, (u32)offsetof(struct chopperobj, target), ivalue);
			}
			if (strcmp(field, "chopper.attack_mode") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, attackmode), ivalue);
			}
			if (strcmp(field, "chopper.clockwise") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, cw), ivalue);
			}
			if (strcmp(field, "chopper.vx") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vx), fvalue);
			}
			if (strcmp(field, "chopper.vy") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vy), fvalue);
			}
			if (strcmp(field, "chopper.vz") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, vz), fvalue);
			}
			if (strcmp(field, "chopper.power") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, power), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_x") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, otx), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_y") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, oty), fvalue);
			}
			if (strcmp(field, "chopper.origin_target_z") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, otz), fvalue);
			}
			if (strcmp(field, "chopper.bob") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, bob), fvalue);
			}
			if (strcmp(field, "chopper.bob_strength") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, bobstrength), fvalue);
			}
			if (strcmp(field, "chopper.target_visible") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, targetvisible), ivalue);
			}
			if (strcmp(field, "chopper.timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS32(record, (u32)offsetof(struct chopperobj, timer60), ivalue);
			}
			if (strcmp(field, "chopper.patrol_timer60") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteS32(record, (u32)offsetof(struct chopperobj, patroltimer60), ivalue);
			}
			if (strcmp(field, "chopper.gun_turn_y_speed60") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunturnyspeed60), fvalue);
			}
			if (strcmp(field, "chopper.gun_turn_x_speed60") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunturnxspeed60), fvalue);
			}
			if (strcmp(field, "chopper.gun_rot_y") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunroty), fvalue);
			}
			if (strcmp(field, "chopper.gun_rot_x") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, gunrotx), fvalue);
			}
			if (strcmp(field, "chopper.barrel_rot_speed") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, barrelrotspeed), fvalue);
			}
			if (strcmp(field, "chopper.barrel_rot") == 0 &&
					s_parseFloat(value, &fvalue)) {
				return s_setupWriteF32(record, (u32)offsetof(struct chopperobj, barrelrot), fvalue);
			}
			if (strcmp(field, "chopper.dead") == 0 &&
					s_parseInteger(value, &ivalue)) {
				return s_setupWriteU8(record, (u32)offsetof(struct chopperobj, dead), ivalue);
			}
		}
		break;
	}
	case OBJTYPE_GLASS:
		if (strcmp(field, "glass.portal") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct glassobj, portalnum), ivalue);
		}
		break;
	case OBJTYPE_PADLOCKEDDOOR:
		if (strcmp(field, "padlocked_door.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct padlockeddoorobj, door),
				ref_record_id);
		}
		if (strcmp(field, "padlocked_door.lock") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct padlockeddoorobj, lock),
				ref_record_id);
		}
		break;
	case OBJTYPE_SAFEITEM:
		if (strcmp(field, "safe_item.item") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, item),
				ref_record_id);
		}
		if (strcmp(field, "safe_item.safe") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, safe),
				ref_record_id);
		}
		if (strcmp(field, "safe_item.door") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct safeitemobj, door),
				ref_record_id);
		}
		break;
	case OBJTYPE_TINTEDGLASS:
		if (strcmp(field, "tinted_glass.xlu_distance") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, xludist), ivalue);
		}
		if ((strcmp(field, "tinted_glass.opaque_distance") == 0 ||
				strcmp(field, "tinted_glass.opa_distance") == 0) &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, opadist), ivalue);
		}
		if (strcmp(field, "tinted_glass.opacity") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, opacity), ivalue);
		}
		if (strcmp(field, "tinted_glass.portal") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct tintedglassobj, portalnum), ivalue);
		}
		if (strcmp(field, "tinted_glass.unknown_64") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct tintedglassobj, unk64), fvalue);
		}
		break;
	case OBJTYPE_LIFT: {
		u32 index;
		char member[32];

		if (sscanf(field, "lift.stops[%u].%31s", &index, member) == 2
				&& index < ARRAYCOUNT(((struct liftobj *)0)->pads)) {
			if (strcmp(member, "pad") == 0) {
				ivalue = s_parseIndexedRef(value);
				return s_setupWriteS16(record,
					(u32)offsetof(struct liftobj, pads) + index * (u32)sizeof(s16),
					ivalue);
			}
			if (strcmp(member, "door") == 0 &&
					type && strcmp(type, "record_ref") == 0) {
				return s_setupWriteRecordRefS32(record,
					(u32)offsetof(struct liftobj, doors)
						+ index * (u32)sizeof(struct doorobj *),
					ref_record_id);
			}
		}
		if (strcmp(field, "lift.distance") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, dist), fvalue);
		}
		if (strcmp(field, "lift.speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, speed), fvalue);
		}
		if (strcmp(field, "lift.accel") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, accel), fvalue);
		}
		if (strcmp(field, "lift.max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct liftobj, maxspeed), fvalue);
		}
		if (strcmp(field, "lift.sound") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, soundtype), ivalue);
		}
		if (strcmp(field, "lift.current_level") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, levelcur), ivalue);
		}
		if (strcmp(field, "lift.target_level") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct liftobj, levelaim), ivalue);
		}
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct liftobj, prevpos),
			"lift.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	}
	case OBJTYPE_CONDITIONALSCENERY:
		if (strcmp(field, "conditional_scenery.trigger") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, trigger),
				ref_record_id);
		}
		if (strcmp(field, "conditional_scenery.unexploded") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, unexp),
				ref_record_id);
		}
		if (strcmp(field, "conditional_scenery.exploded") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct linksceneryobj, exp),
				ref_record_id);
		}
		break;
	case OBJTYPE_BLOCKEDPATH:
		if (strcmp(field, "blocked_path.blocker") == 0 &&
				type && strcmp(type, "record_ref") == 0) {
			return s_setupWriteRecordRefS32(record,
				(u32)offsetof(struct blockedpathobj, blocker),
				ref_record_id);
		}
		if (strcmp(field, "blocked_path.waypoint_1") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct blockedpathobj, waypoint1), ivalue);
		}
		if (strcmp(field, "blocked_path.waypoint_2") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS16(record, (u32)offsetof(struct blockedpathobj, waypoint2), ivalue);
		}
		break;
	case OBJTYPE_CAMERAPOS:
		if (strcmp(field, "camera_position.x") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, x), fvalue);
		}
		if (strcmp(field, "camera_position.y") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, y), fvalue);
		}
		if (strcmp(field, "camera_position.z") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, z), fvalue);
		}
		if (strcmp(field, "camera_position.theta") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, theta), fvalue);
		}
		if (strcmp(field, "camera_position.vertical_angle") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct cameraposobj, verta), fvalue);
		}
		if (strcmp(field, "camera_position.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct cameraposobj, pad), ivalue);
		}
		break;
	case OBJTYPE_HOVERBIKE:
		applied = s_setupApplyHoverField(record,
			(u32)offsetof(struct hoverbikeobj, hov),
			"hover", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speed),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speed),
			"hoverbike.speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, prevpos),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->prevpos),
			"hoverbike.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		if (strcmp(field, "hoverbike.ex_real") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, exreal), fvalue);
		}
		if (strcmp(field, "hoverbike.ez_real") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, ezreal), fvalue);
		}
		if (strcmp(field, "hoverbike.ez_real2") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, ezreal2), fvalue);
		}
		if (strcmp(field, "hoverbike.lean_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, leanspeed), fvalue);
		}
		if (strcmp(field, "hoverbike.lean_diff") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct hoverbikeobj, leandiff), fvalue);
		}
		if (strcmp(field, "hoverbike.max_speed_time240") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct hoverbikeobj, maxspeedtime240), ivalue);
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, rels),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->rels),
			"hoverbike.relative", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speedabs),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speedabs),
			"hoverbike.absolute_speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		applied = s_setupApplyF32ArrayField(record,
			(u32)offsetof(struct hoverbikeobj, speedrel),
			ARRAYCOUNT(((struct hoverbikeobj *)0)->speedrel),
			"hoverbike.relative_speed", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	case OBJTYPE_HOVERPROP:
		applied = s_setupApplyHoverField(record,
			(u32)offsetof(struct hoverpropobj, hov),
			"hover", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	case OBJTYPE_FAN:
		if (strcmp(field, "fan.y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yrot), fvalue);
		}
		if (strcmp(field, "fan.previous_y_rot") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yrotprev), fvalue);
		}
		if (strcmp(field, "fan.y_max_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, ymaxspeed), fvalue);
		}
		if (strcmp(field, "fan.y_speed") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yspeed), fvalue);
		}
		if (strcmp(field, "fan.y_accel") == 0 &&
				s_parseFloat(value, &fvalue)) {
			return s_setupWriteF32(record, (u32)offsetof(struct fanobj, yaccel), fvalue);
		}
		if (strcmp(field, "fan.on") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS8(record, (u32)offsetof(struct fanobj, on), ivalue);
		}
		break;
	case OBJTYPE_PADEFFECT:
		if (strcmp(field, "pad_effect.effect") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct padeffectobj, effect), ivalue);
		}
		if (strcmp(field, "pad_effect.pad") == 0) {
			ivalue = s_parseIndexedRef(value);
			return s_setupWriteS32(record, (u32)offsetof(struct padeffectobj, pad), ivalue);
		}
		break;
	case OBJTYPE_ESCASTEP:
		if (strcmp(field, "escalator_step.frame") == 0 &&
				s_parseInteger(value, &ivalue)) {
			return s_setupWriteS32(record, (u32)offsetof(struct escalatorobj, frame), ivalue);
		}
		applied = s_setupApplyCoordField(record,
			(u32)offsetof(struct escalatorobj, prevpos),
			"escalator_step.previous_position", field, value);
		if (applied >= 0) {
			return applied;
		}
		break;
	default:
		break;
	}

	if (type && strcmp(type, "record_ref") == 0 && ref_record_id
			&& ref_record_id[0]) {
		s_setupTableSetError(table,
			"setup record reference field '%s' is not compiled yet",
			field, "");
		return 0;
	}

	return 0;
}

static s32 s_setupParseFieldsTsv(char *text,
	scenario_source_setup_table_t *table)
{
	char *cursor;
	char *line;
	s32 default_order = 0;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *record_id = s_nextField(&line_cursor);
		char *kind = s_nextField(&line_cursor);
		char *field = s_nextField(&line_cursor);
		char *type = s_nextField(&line_cursor);
		char *value = s_nextField(&line_cursor);
		char *catalog_id = s_nextField(&line_cursor);
		char *ref_record_id = s_nextField(&line_cursor);
		scenario_source_setup_record_t *record;

		if (!record_id || !kind || !field || !type) {
			continue;
		}
		if (strcmp(record_id, "record_id") == 0) {
			continue;
		}

		record = s_setupFindOrAddRecord(table, record_id, kind,
			default_order);
		if (!record) {
			return 0;
		}

		if (!s_setupApplyKnownField(table, record, field, type,
				value ? value : "", catalog_id ? catalog_id : "",
				ref_record_id ? ref_record_id : "")) {
			s_setupTableSetError(table,
				"unsupported setup source field '%s' on '%s'",
				field, record_id);
			return 0;
		}

		default_order++;
	}

	return table->error[0] == '\0';
}

static s32 s_setupApplyObjectsSummary(char *text,
	scenario_source_setup_table_t *table)
{
	char *cursor;
	char *line;
	s32 order = 0;

	if (!text || !table) {
		return 1;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *record_id = s_nextField(&line_cursor);
		char *kind = s_nextField(&line_cursor);
		char *pad_ref = s_nextField(&line_cursor);
		char *model_id = s_nextField(&line_cursor);
		char *weapon_id = s_nextField(&line_cursor);
		char *secondary_weapon_id = s_nextField(&line_cursor);
		char *body_id = s_nextField(&line_cursor);
		char *head_id = s_nextField(&line_cursor);
		char *ailist_ref = s_nextField(&line_cursor);
		char *flags = s_nextField(&line_cursor);
		char *flags2 = s_nextField(&line_cursor);
		char *flags3 = s_nextField(&line_cursor);
		scenario_source_setup_record_t *record;
		s32 ivalue;
		u32 uvalue;

		if (!record_id || !kind) {
			continue;
		}
		if (strcmp(record_id, "record_id") == 0) {
			continue;
		}

		record = s_setupFindOrAddRecord(table, record_id, kind, order);
		if (!record) {
			return 0;
		}

		if (pad_ref && pad_ref[0]) {
			ivalue = s_parseIndexedRef(pad_ref);
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU16(record,
						(u32)offsetof(struct packedchr, padnum), ivalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteS16(record,
						(u32)offsetof(struct defaultobj, pad), ivalue)) {
					return 0;
				}
			}
		}

		if (model_id && model_id[0]
				&& s_setupObjTypeHasDefaultBase(record->type)) {
			if (!s_setupResolveModelnum(model_id, &ivalue) ||
					!s_setupWriteS16(record,
						(u32)offsetof(struct defaultobj, modelnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown model catalog id '%s' on '%s'",
					model_id, record_id);
				return 0;
			}
		}

		if (weapon_id && weapon_id[0]
				&& (record->type == OBJTYPE_WEAPON
					|| record->type == OBJTYPE_MINE)) {
			if (!s_setupResolveWeaponnum(weapon_id, &ivalue) ||
					!s_setupWriteU8(record,
						(u32)offsetof(struct weaponobj, weaponnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown weapon catalog id '%s' on '%s'",
					weapon_id, record_id);
				return 0;
			}
		}

		if (secondary_weapon_id && secondary_weapon_id[0]
				&& (record->type == OBJTYPE_WEAPON
					|| record->type == OBJTYPE_MINE)) {
			if (!s_setupResolveWeaponnum(secondary_weapon_id, &ivalue) ||
					!s_setupWriteS8(record,
						(u32)offsetof(struct weaponobj, dualweaponnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown secondary weapon catalog id '%s' on '%s'",
					secondary_weapon_id, record_id);
				return 0;
			}
		}

		if (body_id && body_id[0] && record->type == OBJTYPE_CHR) {
			if (!s_setupResolveBodyNum(body_id, &ivalue) ||
					!s_setupWriteU8(record,
						(u32)offsetof(struct packedchr, bodynum), ivalue)) {
				s_setupTableSetError(table,
					"unknown body catalog id '%s' on '%s'",
					body_id, record_id);
				return 0;
			}
		}

		if (head_id && head_id[0] && record->type == OBJTYPE_CHR) {
			if (!s_setupResolveHeadNum(head_id, &ivalue) ||
					!s_setupWriteS8(record,
						(u32)offsetof(struct packedchr, headnum), ivalue)) {
				s_setupTableSetError(table,
					"unknown head catalog id '%s' on '%s'",
					head_id, record_id);
				return 0;
			}
		}

		if (ailist_ref && ailist_ref[0] && record->type == OBJTYPE_CHR) {
			ivalue = s_parseIndexedRef(ailist_ref);
			if (!s_setupWriteU16(record,
					(u32)offsetof(struct packedchr, ailistnum), ivalue)) {
				return 0;
			}
		}

		if (flags && flags[0] && s_parseU32Value(flags, &uvalue)) {
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct packedchr, flags), uvalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct defaultobj, flags), uvalue)) {
					return 0;
				}
			}
		}

		if (flags2 && flags2[0] && s_parseU32Value(flags2, &uvalue)) {
			if (record->type == OBJTYPE_CHR) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct packedchr, flags2), uvalue)) {
					return 0;
				}
			} else if (s_setupObjTypeHasDefaultBase(record->type)) {
				if (!s_setupWriteU32(record,
						(u32)offsetof(struct defaultobj, flags2), uvalue)) {
					return 0;
				}
			}
		}

		if (flags3 && flags3[0] && s_parseU32Value(flags3, &uvalue)
				&& s_setupObjTypeHasDefaultBase(record->type)) {
			if (!s_setupWriteU32(record,
					(u32)offsetof(struct defaultobj, flags3), uvalue)) {
				return 0;
			}
		}

		order++;
	}

	return table->error[0] == '\0';
}

static int s_setupCompareRecords(const void *a, const void *b)
{
	const scenario_source_setup_record_t *ra =
		(const scenario_source_setup_record_t *)a;
	const scenario_source_setup_record_t *rb =
		(const scenario_source_setup_record_t *)b;

	if (ra->order < rb->order) {
		return -1;
	}
	if (ra->order > rb->order) {
		return 1;
	}
	return strcmp(ra->id, rb->id);
}

static u8 *s_setupBuildStageBlock(scenario_source_setup_table_t *table,
	scenario_source_spawn_table_t *spawn_table,
	scenario_source_ai_table_t *ai_table,
	scenario_source_path_table_t *path_table,
	const char *scenario_id, s32 *out_size)
{
	u32 intro_offset = (u32)sizeof(struct stagesetup);
	u32 spawn_count = spawn_table ? (u32)spawn_table->count : 0;
	u32 intro_size = spawn_count * 3u * (u32)sizeof(s32) + (u32)sizeof(s32);
	u32 props_offset = (intro_offset + intro_size + 3u) & ~3u;
	u32 props_size = (u32)sizeof(u32);
	u32 path_count = path_table ? (u32)path_table->count : 0;
	u32 path_words = 0;
	u32 paths_offset;
	u32 paths_size;
	u32 path_pads_offset;
	u32 path_pads_size;
	u32 ailists_offset;
	u32 ailists_size;
	u32 ailist_bytes_offset;
	u32 total_size;
	u8 *data;
	struct stagesetup *setup;
	u32 prop_end = PD_BE32((u32)OBJTYPE_END);
	u32 cursor;
	u32 align_mask = (u32)sizeof(uintptr_t) - 1u;

	if (!table || table->count < 0) {
		return NULL;
	}

	for (s32 i = 0; i < table->count; i++) {
		props_size += table->records[i].len;
	}
	if (path_table) {
		for (s32 i = 0; i < path_table->count; i++) {
			path_words += (u32)path_table->rows[i].pad_count + 1u;
		}
	}

	paths_offset = (props_offset + props_size + align_mask) & ~align_mask;
	paths_size = path_count ? (path_count + 1u) *
		(u32)sizeof(struct path) : 0u;
	path_pads_offset = (paths_offset + paths_size + 3u) & ~3u;
	path_pads_size = path_words * (u32)sizeof(s32);
	ailists_offset = (path_pads_offset + path_pads_size +
		align_mask) & ~align_mask;
	ailists_size = (u32)(((ai_table ? ai_table->count : 0) + 1) *
		(s32)sizeof(struct ailist));
	ailist_bytes_offset = (ailists_offset + ailists_size + 3u) & ~3u;
	total_size = ailist_bytes_offset + (ai_table ? ai_table->byte_count : 0);
	data = mempAlloc(total_size, MEMPOOL_STAGE);
	if (!data) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: setup allocation failed for '%s' bytes=%u",
			scenario_id ? scenario_id : "?", (unsigned)total_size);
		return NULL;
	}
	memset(data, 0, total_size);

	setup = (struct stagesetup *)data;
	setup->intro = (s32 *)(uintptr_t)intro_offset;
	setup->props = (u32 *)(uintptr_t)props_offset;
	setup->paths = path_count ? (struct path *)(uintptr_t)paths_offset : NULL;
	setup->ailists = (struct ailist *)(uintptr_t)ailists_offset;

	cursor = intro_offset;
	if (spawn_table) {
		for (s32 i = 0; i < spawn_table->count; i++) {
			s32 cmd[3];
			cmd[0] = INTROCMD_SPAWN;
			cmd[1] = spawn_table->rows[i].padnum;
			cmd[2] = spawn_table->rows[i].team;
			memcpy(data + cursor, cmd, sizeof(cmd));
			cursor += sizeof(cmd);
		}
	}
	{
		s32 intro_end = INTROCMD_END;
		memcpy(data + cursor, &intro_end, sizeof(intro_end));
	}
	cursor = props_offset;
	for (s32 i = 0; i < table->count; i++) {
		memcpy(data + cursor, table->records[i].bytes,
			table->records[i].len);
		cursor += table->records[i].len;
	}
	memcpy(data + cursor, &prop_end, sizeof(prop_end));
	cursor += sizeof(prop_end);

	if (path_count) {
		struct path *runtime_paths = (struct path *)(data + paths_offset);
		u32 path_cursor = path_pads_offset;
		for (s32 i = 0; i < path_table->count; i++) {
			scenario_source_path_row_t *row = &path_table->rows[i];
			s32 *runtime_pads = (s32 *)(data + path_cursor);
			runtime_paths[i].pads = (s32 *)(uintptr_t)path_cursor;
			runtime_paths[i].id = (u8)row->id;
			runtime_paths[i].flags = (u8)row->flags;
			runtime_paths[i].len = (u16)row->pad_count;
			for (s32 j = 0; j < row->pad_count; j++) {
				runtime_pads[j] = row->pads[j];
			}
			runtime_pads[row->pad_count] = -1;
			path_cursor += (u32)(row->pad_count + 1) *
				(u32)sizeof(s32);
		}
	}

	struct ailist *runtime_ailists = (struct ailist *)(data + ailists_offset);
	u32 list_cursor = ailist_bytes_offset;
	if (ai_table) {
		for (s32 i = 0; i < ai_table->count; i++) {
			runtime_ailists[i].id = ai_table->lists[i].id;
			runtime_ailists[i].list = (u8 *)(uintptr_t)list_cursor;
			for (s32 j = 0; j < ai_table->lists[i].count; j++) {
				scenario_source_ai_command_t *cmd =
					&ai_table->lists[i].commands[j];
				data[list_cursor++] = (u8)((cmd->opcode >> 8) & 0xff);
				data[list_cursor++] = (u8)(cmd->opcode & 0xff);
				if (cmd->operand_count) {
					memcpy(data + list_cursor, cmd->operands,
						cmd->operand_count);
					list_cursor += cmd->operand_count;
				}
			}
		}
	}

	if (out_size) {
		*out_size = (s32)list_cursor;
	}
	return data;
}

static const char *s_setupBehaviorLinkKindForType(u8 type)
{
	switch (type) {
	case OBJTYPE_LINKGUNS:           return "linked_guns";
	case OBJTYPE_LINKLIFTDOOR:       return "lift_door_link";
	case OBJTYPE_SAFEITEM:           return "safe_item";
	case OBJTYPE_PADLOCKEDDOOR:      return "padlocked_door";
	case OBJTYPE_CONDITIONALSCENERY: return "conditional_scenery";
	case OBJTYPE_BLOCKEDPATH:        return "blocked_path";
	default:                         return NULL;
	}
}

static s32 s_setupReadS16Field(const scenario_source_setup_record_t *record,
	u32 offset, s32 *out)
{
	s16 value;

	if (!record || !record->bytes || !out || offset > record->len ||
			sizeof(value) > record->len - offset) {
		return 0;
	}

	memcpy(&value, record->bytes + offset, sizeof(value));
	*out = value;
	return 1;
}

static s32 s_setupReadS32Field(const scenario_source_setup_record_t *record,
	u32 offset, s32 *out)
{
	s32 value;

	if (!record || !record->bytes || !out || offset > record->len ||
			sizeof(value) > record->len - offset) {
		return 0;
	}

	memcpy(&value, record->bytes + offset, sizeof(value));
	*out = value;
	return 1;
}

static s32 s_setupTargetOrderFromS16(
	const scenario_source_setup_record_t *record, u32 offset,
	s32 optional_zero, s32 *out)
{
	s32 rel;

	if (!s_setupReadS16Field(record, offset, &rel)) {
		return 0;
	}
	*out = (optional_zero && rel == 0) ? -1 : record->order + rel;
	return 1;
}

static s32 s_setupTargetOrderFromS32(
	const scenario_source_setup_record_t *record, u32 offset,
	s32 optional_zero, s32 *out)
{
	s32 rel;

	if (!s_setupReadS32Field(record, offset, &rel)) {
		return 0;
	}
	*out = (optional_zero && rel == 0) ? -1 : record->order + rel;
	return 1;
}

static void s_setupClearBehaviorLinks(void)
{
	free(s_ActiveScenarioGraphs.setup_links);
	s_ActiveScenarioGraphs.setup_links = NULL;
	s_ActiveScenarioGraphs.setup_link_count = 0;
	s_ActiveScenarioGraphs.setup_link_logged = 0;
}

static s32 s_setupFillBehaviorLink(
	const scenario_source_setup_record_t *record,
	scenario_source_setup_link_t *link)
{
	s32 value;

	if (!record || !link || !s_setupBehaviorLinkKindForType(record->type)) {
		return 0;
	}

	memset(link, 0, sizeof(*link));
	strncpy(link->record_id, record->id, sizeof(link->record_id) - 1);
	strncpy(link->kind, record->kind, sizeof(link->kind) - 1);
	link->type = record->type;
	link->order = record->order;
	link->target[0] = link->target[1] = link->target[2] = -1;
	link->aux[0] = link->aux[1] = -1;

	switch (record->type) {
	case OBJTYPE_LINKGUNS:
		return s_setupTargetOrderFromS16(record,
				(u32)offsetof(struct linkgunsobj, offset1),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS16(record,
				(u32)offsetof(struct linkgunsobj, offset2),
				0, &link->target[1]);
	case OBJTYPE_LINKLIFTDOOR:
		if (!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linkliftdoorobj, door),
				0, &link->target[0]) ||
				!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linkliftdoorobj, lift),
				0, &link->target[1])) {
			return 0;
		}
		if (s_setupReadS32Field(record,
				(u32)offsetof(struct linkliftdoorobj, stopnum),
				&value)) {
			link->aux[0] = value;
		}
		return 1;
	case OBJTYPE_SAFEITEM:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, item),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, safe),
				0, &link->target[1]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct safeitemobj, door),
				0, &link->target[2]);
	case OBJTYPE_PADLOCKEDDOOR:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct padlockeddoorobj, door),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct padlockeddoorobj, lock),
				0, &link->target[1]);
	case OBJTYPE_CONDITIONALSCENERY:
		return s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, trigger),
				0, &link->target[0]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, unexp),
				1, &link->target[1]) &&
			s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct linksceneryobj, exp),
				1, &link->target[2]);
	case OBJTYPE_BLOCKEDPATH:
		if (!s_setupTargetOrderFromS32(record,
				(u32)offsetof(struct blockedpathobj, blocker),
				0, &link->target[0])) {
			return 0;
		}
		if (s_setupReadS16Field(record,
				(u32)offsetof(struct blockedpathobj, waypoint1),
				&value)) {
			link->aux[0] = value;
		}
		if (s_setupReadS16Field(record,
				(u32)offsetof(struct blockedpathobj, waypoint2),
				&value)) {
			link->aux[1] = value;
		}
		return 1;
	default:
		return 0;
	}
}

static s32 s_setupCollectBehaviorLinkSource(
	const asset_entry_t *scenario, const char *source_path,
	const scenario_source_setup_table_t *table)
{
	s32 count = 0;
	s32 index = 0;
	char active_path[FS_MAXPATH + 1];

	if (!scenario || !table || !s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.setup_fields_path,
			active_path, sizeof(active_path))) {
		return 1;
	}

	s_setupClearBehaviorLinks();
	for (s32 i = 0; i < table->count; i++) {
		if (s_setupBehaviorLinkKindForType(table->records[i].type)) {
			count++;
		}
	}
	if (count <= 0) {
		return 1;
	}

	s_ActiveScenarioGraphs.setup_links =
		(scenario_source_setup_link_t *)calloc((size_t)count,
			sizeof(*s_ActiveScenarioGraphs.setup_links));
	if (!s_ActiveScenarioGraphs.setup_links) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.GRAPH: failed to allocate setup behavior link source for '%s' links=%d",
			scenario->id, count);
		return 0;
	}

	for (s32 i = 0; i < table->count; i++) {
		const scenario_source_setup_record_t *record = &table->records[i];
		if (!s_setupBehaviorLinkKindForType(record->type)) {
			continue;
		}
		if (!s_setupFillBehaviorLink(record,
				&s_ActiveScenarioGraphs.setup_links[index])) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.GRAPH: invalid setup behavior link source '%s' record=%s kind=%s",
				source_path ? source_path : active_path,
				record->id, record->kind);
			s_setupClearBehaviorLinks();
			return 0;
		}
		index++;
	}

	s_ActiveScenarioGraphs.setup_link_count = count;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: setup behavior link source '%s' links=%d backend=graph.setup.links+setup.fields.tsv",
		source_path && source_path[0] ? source_path : active_path,
		count);
	return 1;
}

static s32 s_parseSegmentToken(const char *value, const char *prefix,
	u32 *out)
{
	s32 id;
	u32 flags;

	if (!value || !out) {
		return 0;
	}

	id = s_parseIndexedRef(value);
	if (id < 0 || id > (s32)SCENARIO_SEGMENT_ID_MASK) {
		return 0;
	}

	if (prefix && prefix[0] && !s_startsWith(value, prefix)) {
		return 0;
	}

	flags = 0;
	if (strstr(value, "outward")) {
		flags |= SCENARIO_SEGMENT_FLAG_OUTWARD;
	}
	if (strstr(value, "inward")) {
		flags |= SCENARIO_SEGMENT_FLAG_INWARD;
	}

	*out = ((u32)id & SCENARIO_SEGMENT_ID_MASK) | flags;
	return 1;
}

static void s_freeSegmentList(scenario_source_segment_list_t *list)
{
	if (!list) {
		return;
	}
	if (list->values) {
		free(list->values);
	}
	list->values = NULL;
	list->count = 0;
}

static s32 s_parseSegmentList(char *field, const char *prefix,
	scenario_source_segment_list_t *out)
{
	s32 capacity;
	char *cursor;
	char *token;

	if (!out) {
		return 0;
	}
	out->values = NULL;
	out->count = 0;

	if (!field || !field[0]) {
		return 1;
	}

	capacity = 8;
	out->values = malloc((size_t)capacity * sizeof(*out->values));
	if (!out->values) {
		return 0;
	}

	cursor = field;
	while (cursor && *cursor) {
		u32 value;
		char *end = cursor;

		while (*end && *end != ';' && *end != ',') {
			end++;
		}
		if (*end) {
			*end++ = '\0';
		}
		token = cursor;
		while (*token && isspace((unsigned char)*token)) {
			token++;
		}
		cursor = end;

		if (!token[0]) {
			continue;
		}
		if (!s_parseSegmentToken(token, prefix, &value)) {
			s_freeSegmentList(out);
			return 0;
		}
		if (out->count >= capacity) {
			u32 *grown;
			capacity *= 2;
			grown = realloc(out->values,
				(size_t)capacity * sizeof(*out->values));
			if (!grown) {
				s_freeSegmentList(out);
				return 0;
			}
			out->values = grown;
		}
		out->values[out->count++] = value;
	}

	return 1;
}

static void s_pathTableSetError(scenario_source_path_table_t *table,
	const char *fmt, const char *a)
{
	if (!table || table->error[0]) {
		return;
	}
	snprintf(table->error, sizeof(table->error), fmt,
		a ? a : "");
}

static void s_freePathTable(scenario_source_path_table_t *table)
{
	if (!table) {
		return;
	}
	if (table->rows) {
		for (s32 i = 0; i < table->count; i++) {
			free(table->rows[i].pads);
		}
		free(table->rows);
	}
	memset(table, 0, sizeof(*table));
}

static s32 s_parsePathPadList(char *field, s32 **out_pads,
	s32 *out_count)
{
	s32 capacity;
	s32 count;
	s32 *pads;
	char *cursor;

	if (!out_pads || !out_count) {
		return 0;
	}
	*out_pads = NULL;
	*out_count = 0;

	if (!field || !field[0]) {
		return 0;
	}

	capacity = 8;
	count = 0;
	pads = (s32 *)malloc((size_t)capacity * sizeof(*pads));
	if (!pads) {
		return 0;
	}

	cursor = field;
	while (cursor && *cursor) {
		char *token = cursor;
		char *end = cursor;
		s32 padnum;

		while (*end && *end != ';' && *end != ',') {
			end++;
		}
		if (*end) {
			*end++ = '\0';
		}
		cursor = end;

		while (*token && isspace((unsigned char)*token)) {
			token++;
		}
		if (!token[0]) {
			continue;
		}
		padnum = s_parseIndexedRef(token);
		if (padnum < 0 || !s_startsWith(token, "pad_")) {
			free(pads);
			return 0;
		}
		if (count >= capacity) {
			s32 *grown;
			capacity *= 2;
			grown = (s32 *)realloc(pads,
				(size_t)capacity * sizeof(*pads));
			if (!grown) {
				free(pads);
				return 0;
			}
			pads = grown;
		}
		pads[count++] = padnum;
	}

	if (count <= 0) {
		free(pads);
		return 0;
	}

	*out_pads = pads;
	*out_count = count;
	return 1;
}

static s32 s_pathAppendRow(scenario_source_path_table_t *table,
	scenario_source_path_row_t *row)
{
	scenario_source_path_row_t *rows;
	s32 new_capacity;

	if (!table || !row || row->id < 0 || row->id > 0xff ||
			row->flags > 0xffu || !row->pads || row->pad_count <= 0) {
		return 0;
	}

	if (table->count >= table->capacity) {
		new_capacity = table->capacity ? table->capacity * 2 : 16;
		rows = (scenario_source_path_row_t *)realloc(table->rows,
			(size_t)new_capacity * sizeof(*rows));
		if (!rows) {
			s_pathTableSetError(table,
				"out of memory adding path '%s'", "");
			return 0;
		}
		table->rows = rows;
		table->capacity = new_capacity;
	}
	table->rows[table->count++] = *row;
	memset(row, 0, sizeof(*row));
	return 1;
}

static s32 s_loadPathSourceRows(char *text,
	scenario_source_path_table_t *table)
{
	char *cursor;
	char *line;

	if (!text || !table) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *line_cursor = line;
		char *path_ref = s_nextField(&line_cursor);
		char *flags = s_nextField(&line_cursor);
		char *pads = s_nextField(&line_cursor);
		u32 parsed_flags;
		scenario_source_path_row_t row;

		if (!path_ref || !flags || !pads) {
			continue;
		}
		if (strcmp(path_ref, "path_ref") == 0) {
			continue;
		}

		memset(&row, 0, sizeof(row));
		row.id = s_parseIndexedRef(path_ref);
		if (row.id < 0 || row.id > 0xff ||
				!s_startsWith(path_ref, "path_") ||
				!s_parseU32Value(flags, &parsed_flags) ||
				parsed_flags > 0xffu ||
				!s_parsePathPadList(pads, &row.pads,
				&row.pad_count)) {
			free(row.pads);
			s_pathTableSetError(table, "bad path row '%s'", path_ref);
			return 0;
		}
		row.flags = parsed_flags;

		if (!s_pathAppendRow(table, &row)) {
			free(row.pads);
			return 0;
		}
	}

	return table->error[0] == '\0';
}

static s32 s_parsePadRow(char *line, scenario_source_pad_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "pad_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->room = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->room = s_parseRoomRef(field);

	field = s_nextField(&cursor);
	out->liftnum = field ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	out->flags = field ? (u32)strtoul(field, NULL, 0) : 0;

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->pos[i])) return 0;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->up[i])) return 0;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->look[i])) return 0;
	}
	for (i = 0; i < 6; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->bbox[i])) return 0;
	}

	return 1;
}

static u32 s_scenarioPadFlags(u32 flags)
{
	flags &= 0x3ffff;
	flags &= ~(u32)(PADFLAG_INTPOS
		| PADFLAG_UPALIGNTOX | PADFLAG_UPALIGNTOY | PADFLAG_UPALIGNTOZ
		| PADFLAG_UPALIGNINVERT
		| PADFLAG_LOOKALIGNTOX | PADFLAG_LOOKALIGNTOY | PADFLAG_LOOKALIGNTOZ
		| PADFLAG_LOOKALIGNINVERT);
	flags |= PADFLAG_HASBBOXDATA;
	return flags;
}

static void s_writeFloat(u8 **dst, f32 value)
{
	memcpy(*dst, &value, sizeof(value));
	*dst += sizeof(value);
}

static void s_writePadRecord(u8 *dst,
	const scenario_source_pad_row_t *row)
{
	u32 flags;
	u32 room_bits;
	u32 liftnum;
	u32 header;
	s32 i;
	u8 *p;

	flags = s_scenarioPadFlags(row->flags);
	room_bits = (u32)row->room & 0x3ff;
	liftnum = (u32)row->liftnum & 0x0f;
	header = (flags << 14) | (room_bits << 4) | liftnum;

	p = dst;
	memcpy(p, &header, sizeof(header));
	p += sizeof(header);

	for (i = 0; i < 3; i++) s_writeFloat(&p, row->pos[i]);
	for (i = 0; i < 3; i++) s_writeFloat(&p, row->up[i]);
	for (i = 0; i < 3; i++) s_writeFloat(&p, row->look[i]);
	for (i = 0; i < 6; i++) s_writeFloat(&p, row->bbox[i]);
}

static size_t s_alignSize(size_t value, size_t align)
{
	return (value + align - 1u) & ~(align - 1u);
}

static const u8 *g_SourceWidePadFile;
static u32 *g_SourceWidePadOffsets;
static s32 g_SourceWidePadOffsetCount;

static void s_clearSourceWidePadOffsets(void)
{
	g_SourceWidePadFile = NULL;
	g_SourceWidePadOffsets = NULL;
	g_SourceWidePadOffsetCount = 0;
}

u32 *scenarioSourcePadsGetWideOffsets(const u8 *padfiledata, s32 *out_count)
{
	if (out_count) {
		*out_count = 0;
	}

	if (padfiledata && padfiledata == g_SourceWidePadFile &&
			g_SourceWidePadOffsets && g_SourceWidePadOffsetCount >= 0) {
		if (out_count) {
			*out_count = g_SourceWidePadOffsetCount;
		}
		return g_SourceWidePadOffsets;
	}

	return NULL;
}

static u8 *s_buildPadfile(const scenario_source_pad_row_t *rows,
	s32 row_count, const scenario_source_navigation_t *nav, s32 *out_size)
{
	size_t header_size;
	size_t offset_size;
	size_t record_size;
	size_t pad_records_end;
	size_t waypoint_offset;
	size_t waypoint_list_offset;
	size_t waygroup_offset;
	size_t waygroup_waypoints_offset;
	size_t waygroup_neighbours_offset;
	size_t cover_offset;
	size_t total_size;
	s32 waypoint_count;
	s32 waygroup_count;
	s32 cover_count;
	size_t waypoint_list_words;
	size_t waygroup_waypoint_words;
	size_t waygroup_neighbour_words;
	u8 *buf;
	struct padsfileheader *header;
	u16 *offsets;
	u32 *wide_offsets;
	u8 *records;
	s32 i;
	s32 use_wide_offsets;

	if (out_size) {
		*out_size = 0;
	}
	if ((!rows && row_count > 0) || row_count < 0) {
		return NULL;
	}

	header_size = offsetof(struct padsfileheader, padoffsets);
	offset_size = (size_t)row_count * sizeof(u16);
	record_size = sizeof(u32) + (size_t)15 * sizeof(f32);
	pad_records_end = header_size + offset_size +
		(size_t)row_count * record_size;

	use_wide_offsets = pad_records_end > 0xffff;
	if (use_wide_offsets) {
		offset_size = (size_t)row_count * sizeof(u32);
		pad_records_end = header_size + offset_size +
			(size_t)row_count * record_size;
	}

	waypoint_count = nav ? nav->waypoint_count : 0;
	waygroup_count = nav ? nav->waygroup_count : 0;
	cover_count = nav ? nav->cover_count : 0;
	waypoint_list_words = 0;
	waygroup_waypoint_words = 0;
	waygroup_neighbour_words = 0;

	if (nav) {
		for (i = 0; i < waypoint_count; i++) {
			waypoint_list_words +=
				(size_t)nav->waypoints[i].neighbours.count + 1u;
		}
		for (i = 0; i < waygroup_count; i++) {
			waygroup_waypoint_words +=
				(size_t)nav->waygroups[i].waypoints.count + 1u;
			waygroup_neighbour_words +=
				(size_t)nav->waygroups[i].neighbours.count + 1u;
		}
	}

	total_size = s_alignSize(pad_records_end, sizeof(uintptr_t));
	waypoint_offset = total_size;
	total_size += (size_t)(waypoint_count + 1) * sizeof(struct waypoint);
	waypoint_list_offset = total_size;
	total_size += waypoint_list_words * sizeof(u32);
	total_size = s_alignSize(total_size, sizeof(uintptr_t));
	waygroup_offset = total_size;
	total_size += (size_t)(waygroup_count + 1) * sizeof(struct waygroup);
	waygroup_waypoints_offset = total_size;
	total_size += waygroup_waypoint_words * sizeof(u32);
	waygroup_neighbours_offset = total_size;
	total_size += waygroup_neighbour_words * sizeof(u32);
	total_size = s_alignSize(total_size, sizeof(uintptr_t));
	cover_offset = total_size;
	total_size += (size_t)cover_count * sizeof(struct coverdefinition);

	buf = mempAlloc((u32)total_size, MEMPOOL_STAGE);
	if (!buf) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: could not allocate %u bytes for pads.tsv runtime buffer",
			(unsigned)total_size);
		return NULL;
	}
	memset(buf, 0, total_size);

	header = (struct padsfileheader *)buf;
	header->numpads = row_count;
	header->numcovers = cover_count;
	header->waypointsoffset = (uintptr_t)waypoint_offset;
	header->waygroupsoffset = (uintptr_t)waygroup_offset;
	header->coversoffset = cover_count > 0 ? (uintptr_t)cover_offset : 0;

	offsets = use_wide_offsets ? NULL : (u16 *)(buf + header_size);
	wide_offsets = use_wide_offsets ? (u32 *)(buf + header_size) : NULL;
	records = buf + header_size + offset_size;

	for (i = 0; i < row_count; i++) {
		if (wide_offsets) {
			wide_offsets[i] = (u32)(records - buf);
		} else {
			offsets[i] = (u16)(records - buf);
		}
		s_writePadRecord(records, &rows[i]);
		records += record_size;
	}

	if (wide_offsets) {
		g_SourceWidePadFile = buf;
		g_SourceWidePadOffsets = wide_offsets;
		g_SourceWidePadOffsetCount = row_count;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.SOURCE: pads.tsv runtime uses 32-bit source pad offsets for %d pads (%u byte record zone)",
			row_count, (unsigned)pad_records_end);
	}

	{
		struct waypoint *waypoints = (struct waypoint *)(buf + waypoint_offset);
		u32 *neighbours = (u32 *)(buf + waypoint_list_offset);
		size_t list_offset = waypoint_list_offset;

		for (i = 0; i < waypoint_count; i++) {
			s32 j;
			waypoints[i].padnum = nav->waypoints[i].padnum;
			waypoints[i].neighbours = (s32 *)(uintptr_t)list_offset;
			waypoints[i].groupnum = nav->waypoints[i].groupnum;
			waypoints[i].step = nav->waypoints[i].step;
			for (j = 0; j < nav->waypoints[i].neighbours.count; j++) {
				*neighbours++ = nav->waypoints[i].neighbours.values[j];
				list_offset += sizeof(u32);
			}
			*neighbours++ = 0xffffffffu;
			list_offset += sizeof(u32);
		}
		waypoints[waypoint_count].padnum = -1;
		waypoints[waypoint_count].neighbours = NULL;
		waypoints[waypoint_count].groupnum = 0;
		waypoints[waypoint_count].step = 0;
	}

	{
		struct waygroup *waygroups = (struct waygroup *)(buf + waygroup_offset);
		u32 *group_waypoints = (u32 *)(buf + waygroup_waypoints_offset);
		u32 *group_neighbours = (u32 *)(buf + waygroup_neighbours_offset);
		size_t group_waypoints_list_offset = waygroup_waypoints_offset;
		size_t group_neighbours_list_offset = waygroup_neighbours_offset;

		for (i = 0; i < waygroup_count; i++) {
			s32 j;
			waygroups[i].waypoints = (s32 *)(uintptr_t)group_waypoints_list_offset;
			waygroups[i].neighbours = (s32 *)(uintptr_t)group_neighbours_list_offset;
			waygroups[i].step = nav->waygroups[i].step;
			for (j = 0; j < nav->waygroups[i].waypoints.count; j++) {
				*group_waypoints++ = nav->waygroups[i].waypoints.values[j];
				group_waypoints_list_offset += sizeof(u32);
			}
			*group_waypoints++ = 0xffffffffu;
			group_waypoints_list_offset += sizeof(u32);
			for (j = 0; j < nav->waygroups[i].neighbours.count; j++) {
				*group_neighbours++ = nav->waygroups[i].neighbours.values[j];
				group_neighbours_list_offset += sizeof(u32);
			}
			*group_neighbours++ = 0xffffffffu;
			group_neighbours_list_offset += sizeof(u32);
		}
		waygroups[waygroup_count].neighbours = NULL;
		waygroups[waygroup_count].waypoints = NULL;
		waygroups[waygroup_count].step = 0;
	}

	if (cover_count > 0) {
		struct coverdefinition *covers =
			(struct coverdefinition *)(buf + cover_offset);
		for (i = 0; i < cover_count; i++) {
			covers[i].pos.x = nav->covers[i].pos[0];
			covers[i].pos.y = nav->covers[i].pos[1];
			covers[i].pos.z = nav->covers[i].pos[2];
			covers[i].look.x = nav->covers[i].look[0];
			covers[i].look.y = nav->covers[i].look[1];
			covers[i].look.z = nav->covers[i].look[2];
			covers[i].flags = (u16)(nav->covers[i].flags & 0xffffu);
		}
	}

	if (out_size) {
		*out_size = (s32)total_size;
	}
	return buf;
}

static scenario_source_pad_row_t *s_parsePadsTsv(char *text,
	s32 *out_count)
{
	scenario_source_pad_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (out_count) {
		*out_count = 0;
	}
	if (!text) {
		return NULL;
	}

	capacity = 64;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return NULL;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_pad_row_t row;
		if (!s_parsePadRow(line, &row)) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_pad_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				return NULL;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	if (out_count) {
		*out_count = count;
	}
	return rows;
}

static s32 s_parsePortalVertices(char *field,
	struct scenario_source_portal_row *out)
{
	char *cursor = field;
	s32 count = 0;

	if (!field || !out) {
		return 0;
	}
	while (cursor && *cursor) {
		char *part = cursor;
		char *semi = strchr(part, ';');
		char *coord_cursor = part;
		char *x;
		char *y;
		char *z;
		char *comma;
		if (semi) {
			*semi = '\0';
			cursor = semi + 1;
		} else {
			cursor = NULL;
		}
		x = coord_cursor;
		comma = strchr(coord_cursor, ',');
		if (!comma) {
			return 0;
		}
		*comma = '\0';
		y = comma + 1;
		comma = strchr(y, ',');
		if (!comma) {
			return 0;
		}
		*comma = '\0';
		z = comma + 1;
		if (!x || !y || !z || count >= SCENARIO_SOURCE_PORTAL_MAX_VERTICES ||
				!s_parseFloat(x, &out->vertices[count].x) ||
				!s_parseFloat(y, &out->vertices[count].y) ||
				!s_parseFloat(z, &out->vertices[count].z)) {
			return 0;
		}
		count++;
	}
	if (count < 3) {
		return 0;
	}
	out->vertex_count = count;
	return 1;
}

static s32 s_parsePortalRow(char *line, scenario_source_portal_row_t *out)
{
	char *cursor;
	char *field;

	if (!line || !out || !line[0] || s_startsWith(line, "portal_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}
	field = s_nextField(&cursor);
	out->room1 = s_parseRoomRef(field);
	field = s_nextField(&cursor);
	out->room2 = s_parseRoomRef(field);
	field = s_nextField(&cursor);
	out->flags = field ? (u32)strtoul(field, NULL, 0) : 0;
	field = s_nextField(&cursor);
	if (out->room1 <= 0 || out->room2 <= 0 || out->room1 == out->room2 ||
			!s_parsePortalVertices(field, out)) {
		return 0;
	}
	return 1;
}

static scenario_source_portal_row_t *s_parsePortalsTsv(char *text,
	s32 *out_count)
{
	scenario_source_portal_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (out_count) {
		*out_count = 0;
	}
	if (!text) {
		return NULL;
	}

	capacity = 64;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return NULL;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_portal_row_t row;
		if (!s_parsePortalRow(line, &row)) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_portal_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				return NULL;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	if (out_count) {
		*out_count = count;
	}
	return rows;
}

static s32 s_parseWaypointRow(char *line, scenario_source_waypoint_row_t *out)
{
	char *cursor;
	char *field;

	if (!line || !out || !line[0] || s_startsWith(line, "waypoint_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->groupnum = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->padnum = s_parseIndexedRef(field);
	if (out->padnum < 0) {
		return -1;
	}

	field = s_nextField(&cursor);
	out->groupnum = s_parseIndexedRef(field);

	field = s_nextField(&cursor);
	out->step = field && field[0] ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waypoint_", &out->neighbours)) {
		return -1;
	}

	return 1;
}

static s32 s_parseWaygroupRow(char *line, scenario_source_waygroup_row_t *out)
{
	char *cursor;
	char *field;

	if (!line || !out || !line[0] || s_startsWith(line, "waygroup_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->step = field && field[0] ? (s32)strtol(field, NULL, 0) : 0;

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waypoint_", &out->waypoints)) {
		return -1;
	}

	field = s_nextField(&cursor);
	if (!s_parseSegmentList(field, "waygroup_", &out->neighbours)) {
		s_freeSegmentList(&out->waypoints);
		return -1;
	}

	return 1;
}

static s32 s_parseCoverRow(char *line, scenario_source_cover_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "cover_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return 0;
	}

	field = s_nextField(&cursor);
	out->flags = field && field[0] ? (u32)strtoul(field, NULL, 0) : 0;

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->pos[i])) return -1;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->look[i])) return -1;
	}

	return 1;
}

static s32 s_parseVolumeRow(char *line, scenario_source_volume_row_t *out)
{
	char *cursor;
	char *field;
	s32 i;

	if (!line || !out || !line[0] || s_startsWith(line, "volume_id")) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->padnum = -1;
	out->room = -1;

	cursor = line;
	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return -1;
	}
	s_copyString(out->id, sizeof(out->id), field);

	field = s_nextField(&cursor);
	if (field && field[0]) {
		out->padnum = s_parseIndexedRef(field);
	}

	field = s_nextField(&cursor);
	if (!field || !field[0]) {
		return -1;
	}
	s_copyString(out->kind, sizeof(out->kind), field);

	field = s_nextField(&cursor);
	if (field && field[0]) {
		out->room = s_parseRoomRef(field);
	}

	field = s_nextField(&cursor);
	if (!field || strcmp(field, "aabb") != 0) {
		return -1;
	}
	s_copyString(out->shape, sizeof(out->shape), field);

	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->min[i])) return -1;
	}
	for (i = 0; i < 3; i++) {
		field = s_nextField(&cursor);
		if (!s_parseFloat(field, &out->max[i])) return -1;
		if (out->max[i] < out->min[i]) return -1;
	}

	return 1;
}

static s32 s_loadLevelVolumeSourceRows(const char *path,
	scenario_source_volume_row_t **out_rows, s32 *out_count)
{
	scenario_source_volume_row_t *rows;
	s32 capacity;
	s32 count;
	char *text;
	char *cursor;
	char *line;

	if (out_rows) {
		*out_rows = NULL;
	}
	if (out_count) {
		*out_count = 0;
	}
	if (!path || !path[0]) {
		return 0;
	}

	text = s_loadGraphText(path, NULL);
	if (!text) {
		return 0;
	}

	capacity = 32;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		free(text);
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_volume_row_t row;
		s32 parsed = s_parseVolumeRow(line, &row);
		if (parsed < 0) {
			free(rows);
			free(text);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_volume_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				free(text);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	free(text);
	if (count == 0) {
		free(rows);
		rows = NULL;
	}
	if (out_rows) {
		*out_rows = rows;
	}
	if (out_count) {
		*out_count = count;
	}
	return 1;
}

static void s_freeNavigation(scenario_source_navigation_t *nav)
{
	s32 i;

	if (!nav) {
		return;
	}

	if (nav->waypoints) {
		for (i = 0; i < nav->waypoint_count; i++) {
			s_freeSegmentList(&nav->waypoints[i].neighbours);
		}
		free(nav->waypoints);
	}
	if (nav->waygroups) {
		for (i = 0; i < nav->waygroup_count; i++) {
			s_freeSegmentList(&nav->waygroups[i].waypoints);
			s_freeSegmentList(&nav->waygroups[i].neighbours);
		}
		free(nav->waygroups);
	}
	if (nav->covers) {
		free(nav->covers);
	}
	memset(nav, 0, sizeof(*nav));
}

static s32 s_parseWaypointsTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_waypoint_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 32;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_waypoint_row_t row;
		s32 parsed = s_parseWaypointRow(line, &row);
		if (parsed < 0) {
			s32 i;
			for (i = 0; i < count; i++) {
				s_freeSegmentList(&rows[i].neighbours);
			}
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_waypoint_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				s32 i;
				s_freeSegmentList(&row.neighbours);
				for (i = 0; i < count; i++) {
					s_freeSegmentList(&rows[i].neighbours);
				}
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->waypoints = rows;
	nav->waypoint_count = count;
	return 1;
}

static s32 s_parseWaygroupsTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_waygroup_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 16;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_waygroup_row_t row;
		s32 parsed = s_parseWaygroupRow(line, &row);
		if (parsed < 0) {
			s32 i;
			for (i = 0; i < count; i++) {
				s_freeSegmentList(&rows[i].waypoints);
				s_freeSegmentList(&rows[i].neighbours);
			}
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_waygroup_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				s32 i;
				s_freeSegmentList(&row.waypoints);
				s_freeSegmentList(&row.neighbours);
				for (i = 0; i < count; i++) {
					s_freeSegmentList(&rows[i].waypoints);
					s_freeSegmentList(&rows[i].neighbours);
				}
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->waygroups = rows;
	nav->waygroup_count = count;
	return 1;
}

static s32 s_parseCoversTsv(char *text, scenario_source_navigation_t *nav)
{
	scenario_source_cover_row_t *rows;
	s32 capacity;
	s32 count;
	char *cursor;
	char *line;

	if (!text || !nav) {
		return 0;
	}

	capacity = 16;
	count = 0;
	rows = malloc((size_t)capacity * sizeof(*rows));
	if (!rows) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		scenario_source_cover_row_t row;
		s32 parsed = s_parseCoverRow(line, &row);
		if (parsed < 0) {
			free(rows);
			return 0;
		}
		if (parsed == 0) {
			continue;
		}
		if (count >= capacity) {
			scenario_source_cover_row_t *grown;
			capacity *= 2;
			grown = realloc(rows, (size_t)capacity * sizeof(*rows));
			if (!grown) {
				free(rows);
				return 0;
			}
			rows = grown;
		}
		rows[count++] = row;
	}

	nav->covers = rows;
	nav->cover_count = count;
	return 1;
}

static void s_copyMemberPath(char *out, size_t out_n,
	const char *archive_member_path, const char *fallback_member)
{
	const char *sep;
	size_t archive_len;

	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';

	if (!archive_member_path || !archive_member_path[0]
			|| !fallback_member || !fallback_member[0]) {
		return;
	}

	sep = strstr(archive_member_path, "::");
	if (!sep) {
		return;
	}

	archive_len = (size_t)(sep - archive_member_path);
	if (archive_len + 2 + strlen(fallback_member) >= out_n) {
		return;
	}

	memcpy(out, archive_member_path, archive_len);
	out[archive_len] = '\0';
	strcat(out, "::");
	strcat(out, fallback_member);
}

static void s_matchScenarioEntry(const asset_entry_t *entry, void *userdata)
{
	scenario_source_match_t *match;
	s32 mode;
	s32 category_match = 0;

	if (!entry || !userdata) {
		return;
	}

	match = (scenario_source_match_t *)userdata;
	if (!match->stage || entry->ext.scenario.stagenum != match->stage->stagenum) {
		return;
	}

	if (match->stage->entry && match->stage->entry->category[0]) {
		category_match = strncmp(entry->category,
			match->stage->entry->category, CATALOG_CATEGORY_LEN) == 0;
	}

	mode = entry->ext.scenario.mode;
	if (mode == match->desired_mode || mode == 0 || match->desired_mode == 0) {
		if (category_match && !match->category_exact) {
			match->category_exact = entry;
			return;
		}
		if (!match->exact) {
			match->exact = entry;
		}
		return;
	}

	if (category_match && !match->category_fallback) {
		match->category_fallback = entry;
		return;
	}

	if (!match->fallback) {
		match->fallback = entry;
	}
}

static const asset_entry_t *s_findScenarioByDerivedStageId(
	const catalog_stage_result_t *stage)
{
	const char *id;
	const char *colon;
	const char *slug;
	char scenario_id[CATALOG_ID_LEN];
	const asset_entry_t *candidate;
	size_t namespace_len;
	size_t slug_len;
	int written;

	if (!stage || !stage->entry || !stage->entry->id[0]) {
		return NULL;
	}

	id = stage->entry->id;
	colon = strchr(id, ':');
	if (!colon || !colon[1]) {
		return NULL;
	}

	namespace_len = (size_t)(colon - id);
	slug = colon + 1;
	slug_len = strlen(slug);
	if (namespace_len == 0 || slug_len == 0) {
		return NULL;
	}

	written = snprintf(scenario_id, sizeof(scenario_id),
		"%.*s:scenario_%s", (int)namespace_len, id, slug);
	if (written <= 0 || (size_t)written >= sizeof(scenario_id)) {
		return NULL;
	}

	candidate = assetCatalogResolve(scenario_id);
	if (candidate && candidate->type == ASSET_SCENARIO) {
		return candidate;
	}
	return NULL;
}

static const asset_entry_t *s_findScenarioForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp)
{
	scenario_source_match_t match;
	s32 desired_mode;
	const asset_entry_t *derived;

	if (!stage) {
		return NULL;
	}

	desired_mode = prefer_mp ? MAP_MODE_MP : 0;
	if (!desired_mode && stage->entry && stage->entry->type == ASSET_MAP) {
		desired_mode = stage->entry->ext.map.mode;
	}

	memset(&match, 0, sizeof(match));
	match.stage = stage;
	match.desired_mode = desired_mode;

	assetCatalogIterateByType(ASSET_SCENARIO, s_matchScenarioEntry, &match);
	derived = s_findScenarioByDerivedStageId(stage);
	if (match.category_exact) {
		return match.category_exact;
	}
	if (match.exact) {
		return match.exact;
	}
	if (derived) {
		return derived;
	}
	if (match.category_fallback) {
		return match.category_fallback;
	}
	return match.fallback;
}

const asset_entry_t *scenarioSourceFindEntryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp)
{
	return s_findScenarioForStage(stage, prefer_mp);
}

s32 scenarioSourceValidateBackgroundGeometryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp)
{
	static char s_logged_scenario_id[CATALOG_ID_LEN];
	const asset_entry_t *scenario;
	struct colmesh *mesh;
	const char *stageid;
	const char *scene_path;

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		return 0;
	}

	if (!catalogLoadTypedAsset(ASSET_SCENARIO, scenario->id)) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: background geometry cannot activate public scene source '%s'",
				scenario->id);
		}
		return 0;
	}

	mesh = catalogGetLoadedColmesh(scenario->id);
	if (!mesh || mesh->numtris <= 0) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: background geometry public scene '%s' produced no renderable scene mesh",
				scenario->id);
		}
		return 0;
	}

	if (strncmp(s_logged_scenario_id, scenario->id,
			sizeof(s_logged_scenario_id)) != 0) {
		stageid = (stage && stage->entry && stage->entry->id[0])
			? stage->entry->id : "?";
		scene_path = scenario->ext.scenario.scene_file[0]
			? scenario->ext.scenario.scene_file : "scene.glb";
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.SOURCE: validated background scene source '%s' for stage '%s' source=%s tris=%d; native BG renderer can consume scene.glb",
			scenario->id, stageid, scene_path, mesh->numtris);
		strncpy(s_logged_scenario_id, scenario->id,
			sizeof(s_logged_scenario_id) - 1);
		s_logged_scenario_id[sizeof(s_logged_scenario_id) - 1] = '\0';
	}

	return 1;
}

static s32 s_activeGraphPathForScenario(const asset_entry_t *scenario,
	const char *path, char *out, size_t out_n)
{
	if (!scenario || !scenario->id[0] || !path || !path[0]
			|| !out || out_n == 0) {
		return 0;
	}

	if (!s_ActiveScenarioGraphs.level_graph_active
			|| strncmp(s_ActiveScenarioGraphs.scenario_id,
				scenario->id, CATALOG_ID_LEN) != 0) {
		return 0;
	}

	s_copyString(out, out_n, path);
	return out[0] != '\0';
}

static s32 s_scenarioPadsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.pads_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.pads_file[0]) {
		strncpy(out, scenario->ext.scenario.pads_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	s_copyMemberPath(out, out_n, scenario->ext.scenario.scene_file, "pads.tsv");
	return out[0] != '\0';
}

static s32 s_scenarioMemberPath(const asset_entry_t *scenario,
	const char *member, char *out, size_t out_n)
{
	if (!scenario || !member || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (scenario->ext.scenario.pads_file[0]) {
		s_copyMemberPath(out, out_n, scenario->ext.scenario.pads_file,
			member);
	}
	if (!out[0]) {
		s_copyMemberPath(out, out_n, scenario->ext.scenario.scene_file,
			member);
	}
	return out[0] != '\0';
}

static void s_graphFailure(asset_type_e type, const char *asset_id,
	const char *path, const char *reason);

static s32 s_jsonStringValueForKey(const char *text, const char *key,
	char *out, size_t out_n)
{
	char quoted_key[96];
	const char *p;
	const char *colon;
	const char *value;
	size_t i;

	if (!text || !key || !key[0] || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (strlen(key) + 2 >= sizeof(quoted_key)) {
		return 0;
	}

	snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
	p = strstr(text, quoted_key);
	if (!p) {
		return 0;
	}

	colon = strchr(p + strlen(quoted_key), ':');
	if (!colon) {
		return 0;
	}

	value = colon + 1;
	while (*value == ' ' || *value == '\t' || *value == '\r'
			|| *value == '\n') {
		value++;
	}
	if (*value != '"') {
		return 0;
	}
	value++;

	for (i = 0; value[i] && value[i] != '"' && i + 1 < out_n; i++) {
		if (value[i] == '\\' && value[i + 1]) {
			i++;
		}
		out[i] = value[i];
	}
	out[i] = '\0';
	return out[0] != '\0';
}

static s32 s_graphMemberPath(const asset_entry_t *scenario,
	const char *member, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return 0;
	}
	out[0] = '\0';
	if (!member || !member[0]) {
		return 0;
	}

	if (strstr(member, "::")) {
		s_copyString(out, out_n, member);
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, member, out, out_n);
}

static s32 s_bindLevelGraphTablePath(const asset_entry_t *scenario,
	const char *graph_text, const char *graph_path, const char *key,
	char *out, size_t out_n)
{
	char member[160];
	char reason[160];

	if (!s_jsonStringValueForKey(graph_text, key, member, sizeof(member))) {
		snprintf(reason, sizeof(reason), "missing level graph table '%s'",
			key);
		s_graphFailure(ASSET_SCENARIO,
			scenario && scenario->id[0] ? scenario->id : "?",
			graph_path, reason);
		return 0;
	}

	if (!s_graphMemberPath(scenario, member, out, out_n)) {
		snprintf(reason, sizeof(reason), "invalid level graph table '%s'",
			key);
		s_graphFailure(ASSET_SCENARIO,
			scenario && scenario->id[0] ? scenario->id : "?",
			graph_path, reason);
		return 0;
	}
	return 1;
}

static char *s_loadOptionalText(const char *path, u32 *out_size)
{
	u32 size;
	char *text;

	if (out_size) {
		*out_size = 0;
	}
	if (!path || !path[0]) {
		return NULL;
	}

	size = 0;
	text = (char *)fsFileLoad(path, &size);
	if (!text || size == 0) {
		if (text) {
			free(text);
		}
		return NULL;
	}

	if (out_size) {
		*out_size = size;
	}
	return text;
}

static char *s_loadGraphText(const char *path, u32 *out_size)
{
	u32 size;
	char *raw;
	char *text;

	if (out_size) {
		*out_size = 0;
	}
	if (!path || !path[0]) {
		return NULL;
	}

	size = 0;
	raw = (char *)fsFileLoad(path, &size);
	if (!raw || size == 0) {
		if (raw) {
			free(raw);
		}
		return NULL;
	}

	text = (char *)malloc((size_t)size + 1u);
	if (!text) {
		free(raw);
		return NULL;
	}

	memcpy(text, raw, size);
	text[size] = '\0';
	free(raw);

	if (out_size) {
		*out_size = size;
	}
	return text;
}

static s32 s_missionGraphMemberPath(const asset_entry_t *mission,
	const char *member, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return 0;
	}
	out[0] = '\0';
	if (!member || !member[0]) {
		return 0;
	}

	if (strstr(member, "::")) {
		s_copyString(out, out_n, member);
		return out[0] != '\0';
	}

	if (mission && mission->ext.mission.mission_graph_file[0]) {
		s_copyMemberPath(out, out_n,
			mission->ext.mission.mission_graph_file, member);
	}
	return out[0] != '\0';
}

static s32 s_bindMissionGraphObjectivesPath(const asset_entry_t *mission,
	const char *graph_text, char *out, size_t out_n)
{
	char member[160];

	if (!mission || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_jsonStringValueForKey(graph_text, "file", member,
			sizeof(member)) &&
			s_missionGraphMemberPath(mission, member, out, out_n)) {
		return 1;
	}

	if (mission->ext.mission.objectives_file[0]) {
		s_copyString(out, out_n, mission->ext.mission.objectives_file);
		return out[0] != '\0';
	}

	return s_missionGraphMemberPath(mission, "objectives.tsv", out, out_n);
}

static s32 s_parseDifficultyMask(const char *value, u32 *out)
{
	char buf[64];
	char *token;
	u32 bits = 0;

	if (!out) {
		return 0;
	}
	*out = 0;
	if (!value || !value[0]) {
		return 0;
	}
	if (s_parseU32Value(value, out)) {
		return 1;
	}

	strncpy(buf, value, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	for (size_t i = 0; buf[i]; i++) {
		buf[i] = (char)tolower((unsigned char)buf[i]);
		if (buf[i] == '|' || buf[i] == '+' || buf[i] == '/') {
			buf[i] = ',';
		}
	}

	if (strcmp(buf, "all") == 0) {
		*out = DIFFBIT_A | DIFFBIT_SA | DIFFBIT_PA | DIFFBIT_PD;
		return 1;
	}

	token = strtok(buf, ", ");
	while (token) {
		if (strcmp(token, "a") == 0 || strcmp(token, "agent") == 0) {
			bits |= DIFFBIT_A;
		} else if (strcmp(token, "sa") == 0
				|| strcmp(token, "special") == 0
				|| strcmp(token, "special_agent") == 0) {
			bits |= DIFFBIT_SA;
		} else if (strcmp(token, "pa") == 0
				|| strcmp(token, "perfect") == 0
				|| strcmp(token, "perfect_agent") == 0) {
			bits |= DIFFBIT_PA;
		} else if (strcmp(token, "pd") == 0
				|| strcmp(token, "dark") == 0
				|| strcmp(token, "perfect_dark") == 0) {
			bits |= DIFFBIT_PD;
		} else {
			return 0;
		}
		token = strtok(NULL, ", ");
	}

	if (!bits) {
		return 0;
	}
	*out = bits;
	return 1;
}

static void s_freeMissionObjectiveSourceRows(
	scenario_source_objective_node_t *objectives,
	scenario_source_objective_criteria_t *criteria)
{
	free(objectives);
	free(criteria);
}

static s32 s_loadMissionObjectiveSourceRows(const char *path,
	const char *graph_text,
	scenario_source_objective_node_t **out_objectives,
	s32 *out_objective_count,
	scenario_source_objective_criteria_t **out_criteria,
	s32 *out_criteria_count)
{
	char *text;
	char *cursor;
	char *line;
	scenario_source_objective_node_t *objectives = NULL;
	scenario_source_objective_criteria_t *criteria = NULL;
	s32 objective_count = 0;
	s32 objective_capacity = 0;
	s32 criteria_count = 0;
	s32 criteria_capacity = 0;
	s32 current_objective = -1;
	s32 header = 1;

	if (out_objectives) *out_objectives = NULL;
	if (out_objective_count) *out_objective_count = 0;
	if (out_criteria) *out_criteria = NULL;
	if (out_criteria_count) *out_criteria_count = 0;
	if (!path || !path[0]) {
		return 0;
	}

	text = s_loadGraphText(path, NULL);
	if (!text) {
		return 0;
	}

	cursor = text;
	while ((line = s_nextLine(&cursor)) != NULL) {
		char *field_cursor;
		char *objective_id;
		char *kind;
		char *text_token;
		char *difficulty_mask;
		char *graph_node;
		char *scenario_source;
		char *operand_kind;
		char *target_ref;
		char *target_record_ref;
		char *pad_ref;
		char *state_ref;
		char *match_value;
		char *initial_status;
		s32 i;

		if (!line[0]) {
			continue;
		}
		if (header) {
			header = 0;
			continue;
		}

		field_cursor = line;
		objective_id = s_nextField(&field_cursor);
		kind = s_nextField(&field_cursor);
		text_token = s_nextField(&field_cursor);
		difficulty_mask = s_nextField(&field_cursor);
		graph_node = s_nextField(&field_cursor);
		scenario_source = s_nextField(&field_cursor);
		operand_kind = s_nextField(&field_cursor);
		target_ref = s_nextField(&field_cursor);
		target_record_ref = s_nextField(&field_cursor);
		pad_ref = s_nextField(&field_cursor);
		state_ref = s_nextField(&field_cursor);
		match_value = s_nextField(&field_cursor);
		initial_status = s_nextField(&field_cursor);
		(void)scenario_source;

		if (!objective_id || !objective_id[0] || !kind || !kind[0]
				|| !graph_node || !strstr(graph_node,
					"mission.objective")) {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}
		if (graph_text && graph_text[0] && !strstr(graph_text, graph_node)) {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}

		for (i = 0; objective_id[i]; i++) {
			if (!isprint((unsigned char)objective_id[i])) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
		}

		if (strcmp(kind, "objective") == 0) {
			scenario_source_objective_node_t *grown;
			u32 difficulty_bits;

			if (!s_parseDifficultyMask(difficulty_mask,
					&difficulty_bits)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
			if (objective_count >= objective_capacity) {
				objective_capacity = objective_capacity
					? objective_capacity * 2 : 8;
				grown = realloc(objectives,
					(size_t)objective_capacity * sizeof(*objectives));
				if (!grown) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				objectives = grown;
			}
			memset(&objectives[objective_count], 0,
				sizeof(objectives[objective_count]));
			s_copyString(objectives[objective_count].objective_id,
				sizeof(objectives[objective_count].objective_id),
				objective_id);
			s_copyString(objectives[objective_count].graph_node,
				sizeof(objectives[objective_count].graph_node),
				graph_node);
			s_copyString(objectives[objective_count].text_token,
				sizeof(objectives[objective_count].text_token),
				text_token ? text_token : "");
			s_copyString(objectives[objective_count].difficulty_mask,
				sizeof(objectives[objective_count].difficulty_mask),
				difficulty_mask ? difficulty_mask : "");
			objectives[objective_count].difficulty_bits =
				difficulty_bits;
			objectives[objective_count].criteria_start =
				criteria_count;
			current_objective = objective_count;
			objective_count++;
		} else if (s_startsWith(kind, "objective_")) {
			scenario_source_objective_criteria_t *grown;
			u8 type;

			if (current_objective < 0 ||
					!s_setupKindToObjType(kind, &type)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives, criteria);
				return 0;
			}
			if (criteria_count >= criteria_capacity) {
				criteria_capacity = criteria_capacity
					? criteria_capacity * 2 : 16;
				grown = realloc(criteria,
					(size_t)criteria_capacity * sizeof(*criteria));
				if (!grown) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria = grown;
			}
			memset(&criteria[criteria_count], 0,
				sizeof(criteria[criteria_count]));
			s_copyString(criteria[criteria_count].objective_id,
				sizeof(criteria[criteria_count].objective_id),
				objective_id);
			s_copyString(criteria[criteria_count].kind,
				sizeof(criteria[criteria_count].kind), kind);
			s_copyString(criteria[criteria_count].graph_node,
				sizeof(criteria[criteria_count].graph_node),
				graph_node);
			s_copyString(criteria[criteria_count].operand_kind,
				sizeof(criteria[criteria_count].operand_kind),
				operand_kind ? operand_kind : "");
			s_copyString(criteria[criteria_count].target_ref,
				sizeof(criteria[criteria_count].target_ref),
				target_ref ? target_ref : "");
			s_copyString(criteria[criteria_count].target_record_ref,
				sizeof(criteria[criteria_count].target_record_ref),
				target_record_ref ? target_record_ref : "");
			s_copyString(criteria[criteria_count].pad_ref,
				sizeof(criteria[criteria_count].pad_ref),
				pad_ref ? pad_ref : "");
			s_copyString(criteria[criteria_count].state_ref,
				sizeof(criteria[criteria_count].state_ref),
				state_ref ? state_ref : "");
			criteria[criteria_count].type = type;
			criteria[criteria_count].tag_id =
				s_parseIndexedRef(target_ref);
			criteria[criteria_count].pad =
				s_parseIndexedRef(pad_ref);
			if (state_ref && state_ref[0] &&
					!s_parseStageFlagRef(state_ref,
						&criteria[criteria_count]
							.stage_flag_mask)) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if (match_value && match_value[0]) {
				s32 parsed_match;
				if (!s_parseInteger(match_value, &parsed_match)) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria[criteria_count].match_value =
					parsed_match;
			}
			criteria[criteria_count].initial_status = -1;
			if (initial_status && initial_status[0]) {
				s32 parsed_status;
				if (!s_parseInteger(initial_status,
						&parsed_status)) {
					free(text);
					s_freeMissionObjectiveSourceRows(objectives,
						criteria);
					return 0;
				}
				criteria[criteria_count].initial_status =
					parsed_status;
			}
			criteria[criteria_count].runtime_status =
				criteria[criteria_count].initial_status;
			if ((type == OBJECTIVETYPE_DESTROYOBJ ||
					type == OBJECTIVETYPE_COLLECTOBJ ||
					type == OBJECTIVETYPE_THROWOBJ ||
					type == OBJECTIVETYPE_HOLOGRAPH) &&
					criteria[criteria_count].tag_id < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if ((type == OBJECTIVETYPE_ENTERROOM ||
					type == OBJECTIVETYPE_THROWINROOM) &&
					criteria[criteria_count].pad < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if ((type == OBJECTIVETYPE_COMPFLAGS ||
					type == OBJECTIVETYPE_FAILFLAGS) &&
					criteria[criteria_count].stage_flag_mask == 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			if (s_objectiveCriterionUsesGraphStatus(type) &&
					criteria[criteria_count].initial_status < 0) {
				free(text);
				s_freeMissionObjectiveSourceRows(objectives,
					criteria);
				return 0;
			}
			criteria[criteria_count].objective_index =
				current_objective;
			objectives[current_objective].criteria_count++;
			criteria_count++;
		} else {
			free(text);
			s_freeMissionObjectiveSourceRows(objectives, criteria);
			return 0;
		}
	}

	free(text);
	if (objective_count <= 0) {
		s_freeMissionObjectiveSourceRows(objectives, criteria);
		return 0;
	}

	if (out_objectives) *out_objectives = objectives;
	if (out_objective_count) *out_objective_count = objective_count;
	if (out_criteria) *out_criteria = criteria;
	if (out_criteria_count) *out_criteria_count = criteria_count;
	return 1;
}

static void s_graphFailure(asset_type_e type, const char *asset_id,
	const char *path, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(type)) {
		sysFatalError("ASSET.SOURCE_ONLY: %s graph source '%s' at %s is not "
			"usable (%s); refusing legacy mission/setup fallback.",
			assetSourceDebugTypeLabel(type),
			asset_id && asset_id[0] ? asset_id : "?",
			path && path[0] ? path : "(missing)",
			reason && reason[0] ? reason : "invalid graph");
	}

	sysLogPrintf(LOG_WARNING,
		"%s.GRAPH: graph source '%s' at %s is not usable (%s)",
		type == ASSET_MISSION ? "MISSION" : "SCENARIO",
		asset_id && asset_id[0] ? asset_id : "?",
		path && path[0] ? path : "(missing)",
		reason && reason[0] ? reason : "invalid graph");
}

static s32 s_validateGraphText(asset_type_e type, const char *asset_id,
	const char *path, const char *text, const char *schema,
	const char *required_ref)
{
	if (!text || !text[0]) {
		s_graphFailure(type, asset_id, path, "empty file");
		return 0;
	}
	if (!schema || !schema[0] || !strstr(text, schema)) {
		s_graphFailure(type, asset_id, path, "schema mismatch");
		return 0;
	}
	if (required_ref && required_ref[0] && !strstr(text, required_ref)) {
		s_graphFailure(type, asset_id, path, "missing scenario reference");
		return 0;
	}
	if (!strstr(text, "\"nodes\"")) {
		s_graphFailure(type, asset_id, path, "missing nodes");
		return 0;
	}
	if (strstr(text, "\"nodes\": []") || strstr(text, "\"nodes\":[]")) {
		s_graphFailure(type, asset_id, path, "empty nodes");
		return 0;
	}
	if (type == ASSET_MISSION && !strstr(text, "mission.objective")) {
		s_graphFailure(type, asset_id, path,
			"missing mission objective nodes");
		return 0;
	}
	if (type == ASSET_MISSION
			&& !strstr(text, "mission.objective.source")
			&& !strstr(text, "mission.objective.criteria.source")) {
		s_graphFailure(type, asset_id, path,
			"missing executable mission objective source nodes");
		return 0;
	}
	if (!strstr(text, "\"links\"") && !strstr(text, "\"edges\"")) {
		s_graphFailure(type, asset_id, path, "missing links or edges");
		return 0;
	}
	return 1;
}

static s32 s_countTextOccurrences(const char *text, const char *needle)
{
	const char *p;
	s32 count;

	if (!text || !needle || !needle[0]) {
		return 0;
	}

	p = text;
	count = 0;
	while ((p = strstr(p, needle)) != NULL) {
		count++;
		p += strlen(needle);
	}
	return count;
}

static void s_catalogIdToFilenameSlug(const char *id, char *out, size_t out_n)
{
	size_t i;
	size_t j;

	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!id || !id[0]) {
		return;
	}

	for (i = 0, j = 0; id[i] && j + 1 < out_n; i++) {
		char c = id[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9')) {
			out[j++] = c;
		} else {
			out[j++] = '_';
		}
	}
	out[j] = '\0';
}

static s32 s_missionGraphMentionsScenario(const asset_entry_t *mission,
	const char *scenario_id)
{
	char *text;
	u32 text_size;
	s32 match;

	if (!mission || !mission->ext.mission.mission_graph_file[0]
			|| !scenario_id || !scenario_id[0]) {
		return 0;
	}

	text = s_loadGraphText(mission->ext.mission.mission_graph_file, &text_size);
	if (!text) {
		return 0;
	}

	match = strstr(text, scenario_id) != NULL;
	free(text);
	return match;
}

static void s_matchMissionEntry(const asset_entry_t *entry, void *userdata)
{
	scenario_source_mission_match_t *match;

	if (!entry || !userdata) {
		return;
	}

	match = (scenario_source_mission_match_t *)userdata;
	if (!match->scenario || !match->scenario->id[0]) {
		return;
	}

	if (!match->archive_match && match->scenario_slug[0]
			&& strstr(entry->ext.mission.scenario_archive,
				match->scenario_slug)) {
		match->archive_match = entry;
	}

	if (!match->graph_match
			&& s_missionGraphMentionsScenario(entry,
				match->scenario->id)) {
		match->graph_match = entry;
	}
}

static const asset_entry_t *s_findMissionForScenario(
	const asset_entry_t *scenario)
{
	scenario_source_mission_match_t match;
	const asset_entry_t *exact;
	char exact_id[CATALOG_ID_LEN];
	const char *base_prefix = "base:scenario_";

	if (!scenario || !scenario->id[0]) {
		return NULL;
	}

	if (s_startsWith(scenario->id, base_prefix)) {
		snprintf(exact_id, sizeof(exact_id), "base:mission_%s",
			scenario->id + strlen(base_prefix));
		exact = assetCatalogResolve(exact_id);
		if (exact && exact->type == ASSET_MISSION) {
			return exact;
		}
	}

	memset(&match, 0, sizeof(match));
	match.scenario = scenario;
	s_catalogIdToFilenameSlug(scenario->id, match.scenario_slug,
		sizeof(match.scenario_slug));
	assetCatalogIterateByType(ASSET_MISSION, s_matchMissionEntry, &match);

	if (match.graph_match) {
		return match.graph_match;
	}
	return match.archive_match;
}

s32 scenarioSourceActivateGraphsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp)
{
	const asset_entry_t *scenario;
	const asset_entry_t *mission;
	char graph_path[FS_MAXPATH + 1];
	char mission_objectives_path[FS_MAXPATH + 1];
	char *text;
	u32 text_size;
	const char *stageid;
	s32 active;
	s32 mission_objectives;
	s32 mission_criteria;
	s32 mission_phase_node_count;
	s32 level_volume_count;
	s32 level_volume_node_count;
	s32 level_pad_node_count;
	s32 level_global_settings_node_count;
	s32 level_ai_list_node_count;
	s32 level_ai_stop_node_count;
	s32 level_ai_kneel_node_count;
	s32 level_ai_surrender_node_count;
	s32 level_ai_fade_out_node_count;
	s32 level_ai_remove_chr_node_count;
	s32 level_ai_try_sidestep_node_count;
	s32 level_ai_try_jump_out_node_count;
	s32 level_ai_try_run_sideways_node_count;
	s32 level_ai_try_attack_walk_node_count;
	s32 level_ai_try_attack_run_node_count;
	s32 level_ai_try_attack_roll_node_count;
	s32 level_ai_try_attack_stand_node_count;
	s32 level_ai_try_attack_kneel_node_count;
	s32 level_ai_try_attack_lie_node_count;
	s32 level_ai_if_attack_locked_node_count;
	s32 level_ai_if_attacking_node_count;
	s32 level_ai_try_modify_attack_node_count;
	s32 level_ai_face_entity_node_count;
	s32 level_ai_apply_gset_damage_node_count;
	s32 level_ai_chr_damage_chr_node_count;
	s32 level_ai_consider_grenade_throw_node_count;
	s32 level_ai_drop_item_node_count;
	s32 level_ai_try_run_from_target_node_count;
	s32 level_ai_try_jog_to_target_prop_node_count;
	s32 level_ai_try_walk_to_target_prop_node_count;
	s32 level_ai_try_run_to_target_prop_node_count;
	s32 level_ai_try_go_to_cover_prop_node_count;
	s32 level_ai_try_jog_to_chr_node_count;
	s32 level_ai_try_walk_to_chr_node_count;
	s32 level_ai_try_run_to_chr_node_count;
	s32 level_ai_if_can_hear_alarm_node_count;
	s32 level_ai_if_patrolling_node_count;
	s32 level_ai_if_alarm_active_node_count;
	s32 level_ai_if_gas_active_node_count;
	s32 level_ai_if_hears_target_node_count;
	s32 level_ai_if_saw_injury_node_count;
	s32 level_ai_if_saw_death_node_count;
	s32 level_ai_if_los_to_target_node_count;
	s32 level_ai_if_los_to_attack_target_node_count;
	s32 level_ai_if_target_nearly_in_sight_node_count;
	s32 level_ai_if_nearly_in_targets_sight_node_count;
	s32 level_ai_set_pad_preset_to_pad_on_route_to_target_node_count;
	s32 level_ai_if_saw_target_recently_node_count;
	s32 level_ai_if_heard_target_recently_node_count;
	s32 level_ai_if_los_to_chr_node_count;
	s32 level_ai_if_never_been_on_screen_node_count;
	s32 level_ai_if_on_screen_node_count;
	s32 level_ai_if_chr_in_on_screen_room_node_count;
	s32 level_ai_if_room_is_on_screen_node_count;
	s32 level_ai_if_target_aiming_at_me_node_count;
	s32 level_ai_if_near_miss_node_count;
	s32 level_ai_if_sees_suspicious_item_node_count;
	s32 level_ai_if_check_fov_with_target_node_count;
	s32 level_ai_if_target_in_fov_left_node_count;
	s32 level_ai_if_target_out_of_fov_left_node_count;
	s32 level_ai_if_target_in_fov_node_count;
	s32 level_ai_if_target_out_of_fov_node_count;
	s32 level_ai_if_distance_to_target_less_than_node_count;
	s32 level_ai_if_distance_to_target_greater_than_node_count;
	s32 level_ai_if_chr_distance_to_pad_less_than_node_count;
	s32 level_ai_if_chr_distance_to_pad_greater_than_node_count;
	s32 level_ai_if_distance_to_chr_less_than_node_count;
	s32 level_ai_if_distance_to_chr_greater_than_node_count;
	s32 level_ai_if_any_chr_near_self_node_count;
	s32 level_ai_if_distance_from_target_to_pad_less_than_node_count;
	s32 level_ai_if_distance_from_target_to_pad_greater_than_node_count;
	s32 level_ai_if_chr_in_room_node_count;
	s32 level_ai_if_target_in_room_node_count;
	s32 level_ai_if_chr_has_object_node_count;
	s32 level_ai_if_weapon_thrown_node_count;
	s32 level_ai_if_weapon_thrown_on_object_node_count;
	s32 level_ai_if_chr_has_weapon_equipped_node_count;
	s32 level_ai_if_gun_unclaimed_node_count;
	s32 level_ai_if_object_healthy_node_count;
	s32 level_ai_if_chr_activated_object_node_count;
	s32 level_ai_obj_interact_node_count;
	s32 level_ai_destroy_object_node_count;
	s32 level_ai_drop_object_from_chr_node_count;
	s32 level_ai_chr_drop_items_node_count;
	s32 level_ai_chr_drop_weapon_node_count;
	s32 level_ai_give_object_to_chr_node_count;
	s32 level_ai_object_move_to_pad_node_count;
	s32 level_ai_chr_do_animation_node_count;
	s32 level_ai_be_surprised_one_hand_node_count;
	s32 level_ai_be_surprised_look_around_node_count;
	s32 level_ai_be_surprised_surrender_node_count;
	s32 level_ai_random_node_count;
	s32 level_ai_if_random_less_than_node_count;
	s32 level_ai_if_random_greater_than_node_count;
	s32 level_ai_print_node_count;
	s32 level_ai_noop_node_count;
	s32 level_ai_set_list_node_count;
	s32 level_ai_set_return_list_node_count;
	s32 level_ai_set_shot_list_node_count;
	s32 level_ai_return_list_node_count;
	s32 level_ai_set_punch_dodge_list_node_count;
	s32 level_ai_set_shooting_at_me_list_node_count;
	s32 level_ai_set_dark_room_list_node_count;
	s32 level_ai_set_player_dead_list_node_count;
	s32 level_path_node_count;
	s32 level_ai_jog_to_pad_node_count;
	s32 level_ai_goto_pad_preset_node_count;
	s32 level_ai_walk_to_pad_node_count;
	s32 level_ai_run_to_pad_node_count;
	s32 level_ai_set_path_node_count;
	s32 level_ai_start_patrol_node_count;
	s32 level_ai_set_pad_preset_node_count;
	s32 level_ai_chr_set_pad_preset_node_count;
	s32 level_ai_chr_copy_pad_preset_node_count;
	s32 level_ai_set_chr_preset_node_count;
	s32 level_ai_set_chr_target_node_count;
	s32 level_ai_set_morale_node_count;
	s32 level_ai_add_morale_node_count;
	s32 level_ai_chr_add_morale_node_count;
	s32 level_ai_subtract_morale_node_count;
	s32 level_ai_set_alertness_node_count;
	s32 level_ai_add_alertness_node_count;
	s32 level_ai_chr_add_alertness_node_count;
	s32 level_ai_subtract_alertness_node_count;
	s32 level_ai_if_num_arghs_less_than_node_count;
	s32 level_ai_if_num_arghs_greater_than_node_count;
	s32 level_ai_if_num_close_arghs_less_than_node_count;
	s32 level_ai_if_num_close_arghs_greater_than_node_count;
	s32 level_ai_if_chr_health_greater_than_node_count;
	s32 level_ai_if_chr_health_less_than_node_count;
	s32 level_ai_if_chr_shield_less_than_node_count;
	s32 level_ai_if_chr_shield_greater_than_node_count;
	s32 level_ai_if_injured_node_count;
	s32 level_ai_if_shield_damaged_node_count;
	s32 level_ai_if_morale_less_than_node_count;
	s32 level_ai_if_morale_less_than_random_node_count;
	s32 level_ai_if_alertness_node_count;
	s32 level_ai_if_chr_alertness_less_than_node_count;
	s32 level_ai_if_alertness_less_than_random_node_count;
	s32 level_ai_if_idle_node_count;
	s32 level_ai_if_stopped_node_count;
	s32 level_ai_if_chr_dead_node_count;
	s32 level_ai_if_chr_death_animation_finished_node_count;
	s32 level_ai_if_chr_knocked_out_node_count;
	s32 level_ai_if_can_see_target_node_count;
	s32 level_ai_increase_squadron_alertness_node_count;
	s32 level_ai_set_hear_distance_node_count;
	s32 level_ai_set_view_distance_node_count;
	s32 level_ai_set_grenade_probability_node_count;
	s32 level_ai_set_chr_num_node_count;
	s32 level_ai_set_max_damage_node_count;
	s32 level_ai_add_health_node_count;
	s32 level_ai_set_shield_node_count;
	s32 level_ai_set_reaction_speed_node_count;
	s32 level_ai_set_recovery_speed_node_count;
	s32 level_ai_set_accuracy_node_count;
	s32 level_ai_set_dodge_rating_node_count;
	s32 level_ai_set_unarmed_dodge_rating_node_count;
	s32 level_ai_set_action_node_count;
	s32 level_ai_set_team_orders_node_count;
	s32 level_ai_retreat_node_count;
	s32 level_ai_find_cover_node_count;
	s32 level_ai_find_cover_within_dist_node_count;
	s32 level_ai_find_cover_outside_dist_node_count;
	s32 level_ai_go_to_cover_node_count;
	s32 level_ai_check_cover_out_of_sight_node_count;
	s32 level_ai_orbit_target_node_count;
	s32 level_ai_set_chr_preset_to_unalerted_teammate_node_count;
	s32 level_ai_set_squadron_node_count;
	s32 level_ai_face_cover_node_count;
	s32 level_ai_danger_cover_node_count;
	s32 level_ai_release_cover_node_count;
	s32 level_ai_rebuild_teams_node_count;
	s32 level_ai_rebuild_squadrons_node_count;
	s32 level_ai_chr_set_listening_node_count;
	s32 level_ai_if_chr_not_talking_node_count;
	s32 level_ai_if_orders_node_count;
	s32 level_ai_if_has_orders_node_count;
	s32 level_ai_if_chr_in_squadron_doing_action_node_count;
	s32 level_ai_if_chr_listening_node_count;
	s32 level_ai_if_not_listening_node_count;
	s32 level_ai_if_chr_injured_target_node_count;
	s32 level_ai_if_action_node_count;
	s32 level_ai_if_chr_ammo_quantity_less_than_node_count;
	s32 level_ai_if_chr_target_node_count;
	s32 level_ai_if_compare_chr_presets_team_node_count;
	s32 level_ai_if_human_node_count;
	s32 level_ai_if_skedar_node_count;
	s32 level_ai_if_prop_preset_blocking_sight_to_target_node_count;
	s32 level_ai_remove_object_at_prop_preset_node_count;
	s32 level_ai_if_prop_preset_height_less_than_node_count;
	s32 level_ai_set_target_node_count;
	s32 level_ai_if_presets_target_is_not_my_target_node_count;
	s32 level_ai_set_chr_preset_to_chr_near_self_node_count;
	s32 level_ai_set_chr_preset_to_chr_near_pad_node_count;
	s32 level_ai_if_dangerous_object_nearby_node_count;
	s32 level_ai_if_heli_weapons_armed_node_count;
	s32 level_ai_if_hoverbot_next_step_node_count;
	s32 level_ai_shuffle_investigation_terminals_node_count;
	s32 level_ai_set_pad_preset_to_investigation_terminal_node_count;
	s32 level_ai_heli_arm_weapons_node_count;
	s32 level_ai_heli_unarm_weapons_node_count;
	s32 level_ai_if_safety2_less_than_node_count;
	s32 level_ai_if_player_using_cmp_or_ar34_node_count;
	s32 level_ai_detect_enemy_on_same_floor_node_count;
	s32 level_ai_detect_enemy_node_count;
	s32 level_ai_if_safety_less_than_node_count;
	s32 level_ai_if_target_moving_slowly_node_count;
	s32 level_ai_if_target_moving_closer_node_count;
	s32 level_ai_if_target_moving_away_node_count;
	s32 level_ai_if_squadron_is_dead_node_count;
	s32 level_ai_if_true_node_count;
	s32 level_ai_if_num_chrs_in_squadron_greater_than_node_count;
	s32 level_ai_if_natural_anim_node_count;
	s32 level_ai_if_y_node_count;
	s32 level_ai_if_sound_timer_node_count;
	s32 level_ai_if_target_y_difference_less_than_node_count;
	s32 level_ai_try_attack_amount_node_count;
	s32 level_ai_try_start_alarm_node_count;
	s32 level_ai_activate_alarm_node_count;
	s32 level_ai_deactivate_alarm_node_count;
	s32 level_ai_set_flag_node_count;
	s32 level_ai_unset_flag_node_count;
	s32 level_ai_if_has_flag_node_count;
	s32 level_ai_chr_set_flag_node_count;
	s32 level_ai_chr_unset_flag_node_count;
	s32 level_ai_if_chr_has_flag_node_count;
	s32 level_ai_set_stage_flag_node_count;
	s32 level_ai_unset_stage_flag_node_count;
	s32 level_ai_if_stage_flag_eq_node_count;
	s32 level_ai_set_chrflag_node_count;
	s32 level_ai_unset_chrflag_node_count;
	s32 level_ai_if_has_chrflag_node_count;
	s32 level_ai_chr_set_chrflag_node_count;
	s32 level_ai_chr_unset_chrflag_node_count;
	s32 level_ai_if_chr_has_chrflag_node_count;
	s32 level_ai_chr_set_hidden_flag_node_count;
	s32 level_ai_chr_unset_hidden_flag_node_count;
	s32 level_ai_if_chr_has_hidden_flag_node_count;
	s32 level_ai_set_obj_flag_node_count;
	s32 level_ai_unset_obj_flag_node_count;
	s32 level_ai_if_obj_has_flag_node_count;
	s32 level_ai_open_door_node_count;
	s32 level_ai_close_door_node_count;
	s32 level_ai_if_door_state_node_count;
	s32 level_ai_if_object_is_door_node_count;
	s32 level_ai_lock_door_node_count;
	s32 level_ai_unlock_door_node_count;
	s32 level_ai_if_door_locked_node_count;
	s32 level_ai_if_lift_stationary_node_count;
	s32 level_ai_lift_go_to_stop_node_count;
	s32 level_ai_if_lift_at_stop_node_count;
	s32 level_ai_activate_lift_node_count;
	s32 level_ai_if_using_lift_node_count;
	s32 level_ai_configure_rain_node_count;
	s32 level_ai_configure_snow_node_count;
	s32 level_ai_switch_to_alt_sky_node_count;
	s32 level_ai_set_wind_speed_node_count;
	s32 level_ai_set_lights_node_count;
	s32 level_ai_set_room_flag_node_count;
	s32 level_ai_show_cutscene_chrs_node_count;
	s32 level_ai_configure_environment_node_count;
	s32 level_ai_if_distance_to_target2_less_than_node_count;
	s32 level_ai_if_distance_to_target2_greater_than_node_count;
	s32 level_ai_speak_node_count;
	s32 level_ai_play_sound_node_count;
	s32 level_ai_assign_sound_node_count;
	s32 level_ai_audio_mute_channel_node_count;
	s32 level_ai_if_channel_free_node_count;
	s32 level_ai_set_object_sound_volume_node_count;
	s32 level_ai_set_object_sound_volume_by_distance_node_count;
	s32 level_ai_set_object_sound_playing_node_count;
	s32 level_ai_play_repeating_sound_from_object_node_count;
	s32 level_ai_play_sound_from_entity_node_count;
	s32 level_ai_play_repeating_sound_from_pad_node_count;
	s32 level_ai_if_object_sound_volume_less_than_node_count;
	s32 level_ai_play_sound_from_prop_node_count;
	s32 level_ai_play_temporary_primary_track_node_count;
	s32 level_ai_play_x_track_node_count;
	s32 level_ai_stop_x_track_node_count;
	s32 level_ai_play_track_isolated_node_count;
	s32 level_ai_play_default_tracks_node_count;
	s32 level_ai_play_cutscene_track_node_count;
	s32 level_ai_stop_cutscene_track_node_count;
	s32 level_ai_play_temporary_track_node_count;
	s32 level_ai_stop_ambient_track_node_count;
	s32 level_ai_chr_draw_weapon_node_count;
	s32 level_ai_chr_draw_weapon_in_cutscene_node_count;
	s32 level_ai_set_player_force_speed_node_count;
	s32 level_ai_chr_set_invincible_node_count;
	s32 level_ai_if_player_is_invincible_node_count;
	s32 level_ai_if_chr_has_no_gun_node_count;
	s32 level_ai_chr_delete_weapon_node_count;
	s32 level_ai_if_trigger_shot_list_node_count;
	s32 level_ai_end_level_node_count;
	s32 level_ai_end_cutscene_node_count;
	s32 level_ai_warp_jo_to_pad_node_count;
	s32 level_ai_set_camera_animation_node_count;
	s32 level_ai_if_in_cutscene_node_count;
	s32 level_ai_if_cutscene_button_pressed_node_count;
	s32 level_ai_reorient_for_cutscene_stop_node_count;
	s32 level_ai_warp_jo_to_tag_node_count;
	s32 level_ai_revoke_control_node_count;
	s32 level_ai_grant_control_node_count;
	s32 level_ai_player_fade_in_node_count;
	s32 level_ai_players_fade_out_node_count;
	s32 level_ai_if_colour_fade_complete_node_count;
	s32 level_ai_prepare_warp_orbit_node_count;
	s32 level_ai_begin_warp_latch_node_count;
	s32 level_ai_if_warp_latch_complete_node_count;
	s32 level_ai_spawn_chr_at_pad_node_count;
	s32 level_ai_spawn_chr_at_chr_node_count;
	s32 level_ai_try_equip_weapon_node_count;
	s32 level_ai_try_equip_hat_node_count;
	s32 level_ai_set_obj_image_node_count;
	s32 level_ai_object_do_animation_node_count;
	s32 level_ai_set_door_open_node_count;
	s32 level_ai_duplicate_chr_node_count;
	s32 level_ai_enable_chr_node_count;
	s32 level_ai_disable_chr_node_count;
	s32 level_ai_enable_obj_node_count;
	s32 level_ai_disable_obj_node_count;
	s32 level_ai_chr_move_to_pad_node_count;
	s32 level_ai_chr_set_team_node_count;
	s32 level_ai_damage_chr_by_amount_node_count;
	s32 level_ai_do_preset_animation_node_count;
	s32 level_ai_if_player_chr_portal_distance_less_than_node_count;
	s32 level_ai_if_chr_reposition_valid_node_count;
	s32 level_ai_do_gun_command_node_count;
	s32 level_ai_if_distance_to_gun_less_than_node_count;
	s32 level_ai_recover_gun_node_count;
	s32 level_ai_chr_copy_properties_node_count;
	s32 level_ai_player_auto_walk_node_count;
	s32 level_ai_if_player_auto_walk_finished_node_count;
	s32 level_ai_if_obj_in_room_node_count;
	s32 level_ai_if_player_looking_at_object_node_count;
	s32 level_ai_if_target_is_player_node_count;
	s32 level_ai_chr_kill_node_count;
	s32 level_ai_remove_weapon_from_inventory_node_count;
	s32 level_ai_clear_inventory_node_count;
	s32 level_ai_release_object_node_count;
	s32 level_ai_chr_grab_object_node_count;
	s32 level_ai_toggle_p1p2_node_count;
	s32 level_ai_chr_set_p1p2_node_count;
	s32 level_ai_chr_set_cloaked_node_count;
	s32 level_ai_set_autogun_target_team_node_count;
	s32 level_ai_if_objective_complete_node_count;
	s32 level_ai_if_objective_failed_node_count;
	s32 level_ai_if_all_objectives_complete_node_count;
	s32 level_ai_if_difficulty_less_than_node_count;
	s32 level_ai_if_difficulty_greater_than_node_count;
	s32 level_ai_if_stage_timer_less_than_node_count;
	s32 level_ai_if_stage_timer_greater_than_node_count;
	s32 level_ai_if_stage_id_less_than_node_count;
	s32 level_ai_if_stage_id_greater_than_node_count;
	s32 level_ai_if_waypoint_within_quadrant_node_count;
	s32 level_ai_set_pad_preset_to_target_quadrant_node_count;
	s32 level_ai_if_num_players_less_than_node_count;
	s32 level_ai_if_kill_count_greater_than_node_count;
	s32 level_ai_if_num_knocked_out_chrs_node_count;
	s32 level_ai_kill_bond_node_count;
	s32 level_ai_if_pouncebits_eq_node_count;
	s32 level_ai_if_training_pc_holographed_node_count;
	s32 level_ai_if_player_using_device_node_count;
	s32 level_ai_chr_begin_or_end_teleport_node_count;
	s32 level_ai_if_chr_teleport_full_white_node_count;
	s32 level_ai_chr_set_cutscene_weapon_node_count;
	s32 level_ai_fade_screen_node_count;
	s32 level_ai_if_fade_complete_node_count;
	s32 level_ai_set_chr_hudpiece_visible_node_count;
	s32 level_ai_set_passive_mode_node_count;
	s32 level_ai_chr_set_firing_in_cutscene_node_count;
	s32 level_ai_set_portal_flag_node_count;
	s32 level_ai_if_music_event_queue_is_empty_node_count;
	s32 level_ai_if_coop_mode_node_count;
	s32 level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count;
	s32 level_ai_remove_references_to_chr_node_count;
	s32 level_ai_chr_toggle_model_part_node_count;
	s32 level_ai_obj_set_model_part_visible_node_count;
	s32 level_ai_if_obj_health_less_than_node_count;
	s32 level_ai_set_obj_health_node_count;
	s32 level_ai_set_chr_special_death_animation_node_count;
	s32 level_ai_set_room_to_search_node_count;
	s32 level_ai_restart_timer_node_count;
	s32 level_ai_reset_timer_node_count;
	s32 level_ai_pause_timer_node_count;
	s32 level_ai_resume_timer_node_count;
	s32 level_ai_if_timer_stopped_node_count;
	s32 level_ai_if_timer_greater_than_random_node_count;
	s32 level_ai_if_timer_less_than_node_count;
	s32 level_ai_if_timer_greater_than_node_count;
	s32 level_ai_show_countdown_timer_node_count;
	s32 level_ai_hide_countdown_timer_node_count;
	s32 level_ai_set_countdown_timer_node_count;
	s32 level_ai_stop_countdown_timer_node_count;
	s32 level_ai_start_countdown_timer_node_count;
	s32 level_ai_if_countdown_timer_stopped_node_count;
	s32 level_ai_if_countdown_timer_less_than_node_count;
	s32 level_ai_if_countdown_timer_greater_than_node_count;
	s32 level_ai_set_savefile_flag_node_count;
	s32 level_ai_unset_savefile_flag_node_count;
	s32 level_ai_if_savefile_flag_set_node_count;
	s32 level_ai_if_savefile_flag_unset_node_count;
	s32 level_ai_show_hudmsg_node_count;
	s32 level_ai_show_hudmsg_middle_node_count;
	s32 level_ai_show_hudmsg_top_middle_node_count;
	s32 level_ai_hovercar_begin_path_node_count;
	s32 level_ai_set_vehicle_speed_node_count;
	s32 level_ai_set_rotor_speed_node_count;
	s32 level_ai_chr_explosions_node_count;
	s32 level_ai_set_tinted_glass_enabled_node_count;
	s32 level_ai_hovercopter_fire_rocket_node_count;
	s32 level_ai_chr_adjust_motion_blur_node_count;
	s32 level_ai_punch_or_kick_node_count;
	s32 level_ai_set_target_to_eyespy_if_in_sight_node_count;
	s32 level_ai_mini_skedar_try_pounce_node_count;
	s32 level_ai_if_object_distance_to_pad_less_than_node_count;
	s32 level_ai_avoid_node_count;
	s32 level_ai_title_init_mode_node_count;
	s32 level_ai_try_exit_title_node_count;
	s32 level_ai_chr_emit_sparks_node_count;
	s32 level_ai_set_dr_caroll_images_node_count;
	s32 level_ai_say_quip_node_count;
	s32 level_ai_say_ci_staff_quip_node_count;
	s32 level_ai_shuffle_ruins_pillars_node_count;
	s32 level_ai_shuffle_pelagic_switches_node_count;
	scenario_source_volume_row_t *level_volume_rows;
	scenario_source_objective_node_t *mission_objective_rows;
	scenario_source_objective_criteria_t *mission_criteria_rows;

	s_resetActiveScenarioGraphs();
	level_volume_rows = NULL;
	mission_objective_rows = NULL;
	mission_criteria_rows = NULL;

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			stageid = stage && stage->entry && stage->entry->id[0]
				? stage->entry->id : "?";
			s_graphFailure(ASSET_SCENARIO, stageid, NULL,
				"no scenario catalog entry");
		}
		return 0;
	}

	graph_path[0] = '\0';
	if (scenario->ext.scenario.level_graph_file[0]) {
		s_copyString(graph_path, sizeof(graph_path),
			scenario->ext.scenario.level_graph_file);
	} else {
		s_scenarioMemberPath(scenario, "level.graph.json",
			graph_path, sizeof(graph_path));
	}

	if (!graph_path[0]) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, NULL,
			"missing level.graph.json binding");
		return 0;
	}

	text_size = 0;
	text = s_loadGraphText(graph_path, &text_size);
	if (!text) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"load failed");
		return 0;
	}

	if (!s_validateGraphText(ASSET_SCENARIO, scenario->id, graph_path,
			text, "pd2.level.graph.v1", scenario->id)) {
		free(text);
		return 0;
	}
	level_volume_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.trigger.volume.source\"");
	level_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.pads.source\"");
	level_global_settings_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.global.settings.source\"");
	level_ai_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.lists.source\"");
	level_ai_stop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.stop\"");
	level_ai_kneel_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.kneel\"");
	level_ai_surrender_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.surrender\"");
	level_ai_fade_out_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.fade_out\"");
	level_ai_remove_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.remove_chr\"");
	level_ai_try_sidestep_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_sidestep\"");
	level_ai_try_jump_out_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_jump_out\"");
	level_ai_try_run_sideways_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_run_sideways\"");
	level_ai_try_attack_walk_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_walk\"");
	level_ai_try_attack_run_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_run\"");
	level_ai_try_attack_roll_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_roll\"");
	level_ai_try_attack_stand_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_stand\"");
	level_ai_try_attack_kneel_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_kneel\"");
	level_ai_try_attack_lie_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_lie\"");
	level_ai_if_attack_locked_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_attack_locked\"");
	level_ai_if_attacking_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_attacking\"");
	level_ai_try_modify_attack_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_modify_attack\"");
	level_ai_face_entity_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.face_entity\"");
	level_ai_apply_gset_damage_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.apply_gset_damage\"");
	level_ai_chr_damage_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_damage_chr\"");
	level_ai_consider_grenade_throw_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.consider_grenade_throw\"");
	level_ai_drop_item_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.drop_item\"");
	level_ai_try_run_from_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_run_from_target\"");
	level_ai_try_jog_to_target_prop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_jog_to_target_prop\"");
	level_ai_try_walk_to_target_prop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_walk_to_target_prop\"");
	level_ai_try_run_to_target_prop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_run_to_target_prop\"");
	level_ai_try_go_to_cover_prop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_go_to_cover_prop\"");
	level_ai_try_jog_to_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_jog_to_chr\"");
	level_ai_try_walk_to_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_walk_to_chr\"");
	level_ai_try_run_to_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_run_to_chr\"");
	level_ai_if_can_hear_alarm_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_can_hear_alarm\"");
	level_ai_if_patrolling_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_patrolling\"");
	level_ai_if_alarm_active_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_alarm_active\"");
	level_ai_if_gas_active_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_gas_active\"");
	level_ai_if_hears_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_hears_target\"");
	level_ai_if_saw_injury_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_saw_injury\"");
	level_ai_if_saw_death_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_saw_death\"");
	level_ai_if_los_to_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_los_to_target\"");
	level_ai_if_los_to_attack_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_los_to_attack_target\"");
	level_ai_if_target_nearly_in_sight_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_nearly_in_sight\"");
	level_ai_if_nearly_in_targets_sight_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_nearly_in_targets_sight\"");
	level_ai_set_pad_preset_to_pad_on_route_to_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\"");
	level_ai_if_saw_target_recently_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_saw_target_recently\"");
	level_ai_if_heard_target_recently_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_heard_target_recently\"");
	level_ai_if_los_to_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_los_to_chr\"");
	level_ai_if_never_been_on_screen_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_never_been_on_screen\"");
	level_ai_if_on_screen_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_on_screen\"");
	level_ai_if_chr_in_on_screen_room_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_in_on_screen_room\"");
	level_ai_if_room_is_on_screen_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_room_is_on_screen\"");
	level_ai_if_target_aiming_at_me_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_aiming_at_me\"");
	level_ai_if_near_miss_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_near_miss\"");
	level_ai_if_sees_suspicious_item_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_sees_suspicious_item\"");
	level_ai_if_check_fov_with_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_check_fov_with_target\"");
	level_ai_if_target_in_fov_left_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_in_fov_left\"");
	level_ai_if_target_out_of_fov_left_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_out_of_fov_left\"");
	level_ai_if_target_in_fov_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_target_in_fov\"");
	level_ai_if_target_out_of_fov_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_target_out_of_fov\"");
	level_ai_if_distance_to_target_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_target_less_than\"");
	level_ai_if_distance_to_target_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_target_greater_than\"");
	level_ai_if_chr_distance_to_pad_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_less_than\"");
	level_ai_if_chr_distance_to_pad_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_distance_to_pad_greater_than\"");
	level_ai_if_distance_to_chr_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_chr_less_than\"");
	level_ai_if_distance_to_chr_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_chr_greater_than\"");
	level_ai_if_any_chr_near_self_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_any_chr_near_self\"");
	level_ai_if_distance_from_target_to_pad_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_less_than\"");
	level_ai_if_distance_from_target_to_pad_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\"");
	level_ai_if_chr_in_room_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_in_room\"");
	level_ai_if_target_in_room_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_target_in_room\"");
	level_ai_if_chr_has_object_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_has_object\"");
	level_ai_if_weapon_thrown_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_weapon_thrown\"");
	level_ai_if_weapon_thrown_on_object_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_weapon_thrown_on_object\"");
	level_ai_if_chr_has_weapon_equipped_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_has_weapon_equipped\"");
	level_ai_if_gun_unclaimed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_gun_unclaimed\"");
	level_ai_if_object_healthy_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_object_healthy\"");
	level_ai_if_chr_activated_object_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_activated_object\"");
	level_ai_obj_interact_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.obj_interact\"");
	level_ai_destroy_object_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.destroy_object\"");
	level_ai_drop_object_from_chr_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.drop_object_from_chr\"");
	level_ai_chr_drop_items_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_drop_items\"");
	level_ai_chr_drop_weapon_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_drop_weapon\"");
	level_ai_give_object_to_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.give_object_to_chr\"");
	level_ai_object_move_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.object_move_to_pad\"");
	level_ai_chr_do_animation_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_do_animation\"");
	level_ai_be_surprised_one_hand_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.be_surprised_one_hand\"");
	level_ai_be_surprised_look_around_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.be_surprised_look_around\"");
	level_ai_be_surprised_surrender_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.be_surprised_surrender\"");
	level_ai_random_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.random\"");
	level_ai_if_random_less_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_random_less_than\"");
	level_ai_if_random_greater_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_random_greater_than\"");
	level_ai_print_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.print\"");
	level_ai_noop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.noop\"");
	level_ai_set_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_list\"");
	level_ai_set_return_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_return_list\"");
	level_ai_set_shot_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_shot_list\"");
	level_ai_return_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.return_list\"");
	level_ai_set_punch_dodge_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_punch_dodge_list\"");
	level_ai_set_shooting_at_me_list_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_shooting_at_me_list\"");
	level_ai_set_dark_room_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_dark_room_list\"");
	level_ai_set_player_dead_list_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_player_dead_list\"");
	level_path_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.navigation.paths.source\"");
	level_ai_jog_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.jog_to_pad\"");
	level_ai_goto_pad_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.go_to_pad_preset\"");
	level_ai_walk_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.walk_to_pad\"");
	level_ai_run_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.run_to_pad\"");
	level_ai_set_path_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_path\"");
	level_ai_start_patrol_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.start_patrol\"");
	level_ai_set_pad_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_pad_preset\"");
	level_ai_chr_set_pad_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_pad_preset\"");
	level_ai_chr_copy_pad_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_copy_pad_preset\"");
	level_ai_set_chr_preset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_chr_preset\"");
	level_ai_set_chr_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_chr_target\"");
	level_ai_set_morale_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_morale\"");
	level_ai_add_morale_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.add_morale\"");
	level_ai_chr_add_morale_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_add_morale\"");
	level_ai_subtract_morale_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.subtract_morale\"");
	level_ai_set_alertness_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_alertness\"");
	level_ai_add_alertness_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.add_alertness\"");
	level_ai_chr_add_alertness_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_add_alertness\"");
	level_ai_subtract_alertness_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.subtract_alertness\"");
	level_ai_if_num_arghs_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_arghs_less_than\"");
	level_ai_if_num_arghs_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_arghs_greater_than\"");
	level_ai_if_num_close_arghs_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_close_arghs_less_than\"");
	level_ai_if_num_close_arghs_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_close_arghs_greater_than\"");
	level_ai_if_chr_health_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_health_greater_than\"");
	level_ai_if_chr_health_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_health_less_than\"");
	level_ai_if_chr_shield_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_shield_less_than\"");
	level_ai_if_chr_shield_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_shield_greater_than\"");
	level_ai_if_injured_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_injured\"");
	level_ai_if_shield_damaged_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_shield_damaged\"");
	level_ai_if_morale_less_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_morale_less_than\"");
	level_ai_if_morale_less_than_random_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_morale_less_than_random\"");
	level_ai_if_alertness_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_alertness\"");
	level_ai_if_chr_alertness_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_alertness_less_than\"");
	level_ai_if_alertness_less_than_random_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_alertness_less_than_random\"");
	level_ai_if_idle_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_idle\"");
	level_ai_if_stopped_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_stopped\"");
	level_ai_if_chr_dead_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_dead\"");
	level_ai_if_chr_death_animation_finished_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_death_animation_finished\"");
	level_ai_if_chr_knocked_out_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_knocked_out\"");
	level_ai_if_can_see_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_can_see_target\"");
	level_ai_increase_squadron_alertness_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.increase_squadron_alertness\"");
	level_ai_set_hear_distance_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_hear_distance\"");
	level_ai_set_view_distance_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_view_distance\"");
	level_ai_set_grenade_probability_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_grenade_probability\"");
	level_ai_set_chr_num_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_chr_num\"");
	level_ai_set_max_damage_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_max_damage\"");
	level_ai_add_health_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.add_health\"");
	level_ai_set_shield_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_shield\"");
	level_ai_set_reaction_speed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_reaction_speed\"");
	level_ai_set_recovery_speed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_recovery_speed\"");
	level_ai_set_accuracy_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_accuracy\"");
	level_ai_set_dodge_rating_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_dodge_rating\"");
	level_ai_set_unarmed_dodge_rating_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_unarmed_dodge_rating\"");
	level_ai_set_action_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_action\"");
	level_ai_set_team_orders_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_team_orders\"");
	level_ai_retreat_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.retreat\"");
	level_ai_find_cover_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.find_cover\"");
	level_ai_find_cover_within_dist_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.find_cover_within_dist\"");
	level_ai_find_cover_outside_dist_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.find_cover_outside_dist\"");
	level_ai_go_to_cover_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.go_to_cover\"");
	level_ai_check_cover_out_of_sight_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.check_cover_out_of_sight\"");
	level_ai_orbit_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.orbit_target\"");
	level_ai_set_chr_preset_to_unalerted_teammate_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_chr_preset_to_unalerted_teammate\"");
	level_ai_set_squadron_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_squadron\"");
	level_ai_face_cover_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.face_cover\"");
	level_ai_danger_cover_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.danger_cover\"");
	level_ai_release_cover_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.release_cover\"");
	level_ai_rebuild_teams_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.rebuild_teams\"");
	level_ai_rebuild_squadrons_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.rebuild_squadrons\"");
	level_ai_chr_set_listening_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_listening\"");
	level_ai_if_chr_not_talking_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_not_talking\"");
	level_ai_if_orders_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_orders\"");
	level_ai_if_has_orders_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_has_orders\"");
	level_ai_if_chr_in_squadron_doing_action_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_in_squadron_doing_action\"");
	level_ai_if_chr_listening_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_listening\"");
	level_ai_if_not_listening_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_not_listening\"");
	level_ai_if_chr_injured_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_injured_target\"");
	level_ai_if_action_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_action\"");
	level_ai_if_chr_ammo_quantity_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_ammo_quantity_less_than\"");
	level_ai_if_chr_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_target\"");
	level_ai_if_compare_chr_presets_team_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_compare_chr_presets_team\"");
	level_ai_if_human_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_human\"");
	level_ai_if_skedar_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_skedar\"");
	level_ai_if_prop_preset_blocking_sight_to_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\"");
	level_ai_remove_object_at_prop_preset_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.remove_object_at_prop_preset\"");
	level_ai_if_prop_preset_height_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_prop_preset_height_less_than\"");
	level_ai_set_target_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_target\"");
	level_ai_if_presets_target_is_not_my_target_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_presets_target_is_not_my_target\"");
	level_ai_set_chr_preset_to_chr_near_self_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_self\"");
	level_ai_set_chr_preset_to_chr_near_pad_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_chr_preset_to_chr_near_pad\"");
	level_ai_if_dangerous_object_nearby_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_dangerous_object_nearby\"");
	level_ai_if_heli_weapons_armed_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_heli_weapons_armed\"");
	level_ai_if_hoverbot_next_step_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_hoverbot_next_step\"");
	level_ai_shuffle_investigation_terminals_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.shuffle_investigation_terminals\"");
	level_ai_set_pad_preset_to_investigation_terminal_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_pad_preset_to_investigation_terminal\"");
	level_ai_heli_arm_weapons_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.heli_arm_weapons\"");
	level_ai_heli_unarm_weapons_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.heli_unarm_weapons\"");
	level_ai_if_safety2_less_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_safety2_less_than\"");
	level_ai_if_player_using_cmp_or_ar34_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_using_cmp_or_ar34\"");
	level_ai_detect_enemy_on_same_floor_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.detect_enemy_on_same_floor\"");
	level_ai_detect_enemy_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.detect_enemy\"");
	level_ai_if_safety_less_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_safety_less_than\"");
	level_ai_if_target_moving_slowly_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_moving_slowly\"");
	level_ai_if_target_moving_closer_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_moving_closer\"");
	level_ai_if_target_moving_away_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_moving_away\"");
	level_ai_if_squadron_is_dead_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_squadron_is_dead\"");
	level_ai_if_true_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_true\"");
	level_ai_if_num_chrs_in_squadron_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\"");
	level_ai_if_natural_anim_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_natural_anim\"");
	level_ai_if_y_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_y\"");
	level_ai_if_sound_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_sound_timer\"");
	level_ai_if_target_y_difference_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_y_difference_less_than\"");
	level_ai_try_attack_amount_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_attack_amount\"");
	level_ai_try_start_alarm_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_start_alarm\"");
	level_ai_activate_alarm_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.activate_alarm\"");
	level_ai_deactivate_alarm_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.deactivate_alarm\"");
	level_ai_set_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_flag\"");
	level_ai_unset_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unset_flag\"");
	level_ai_if_has_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_has_flag\"");
	level_ai_chr_set_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_flag\"");
	level_ai_chr_unset_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_unset_flag\"");
	level_ai_if_chr_has_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_chr_has_flag\"");
	level_ai_set_stage_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_stage_flag\"");
	level_ai_unset_stage_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unset_stage_flag\"");
	level_ai_if_stage_flag_eq_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_stage_flag_eq\"");
	level_ai_set_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_chrflag\"");
	level_ai_unset_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unset_chrflag\"");
	level_ai_if_has_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_has_chrflag\"");
	level_ai_chr_set_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_chrflag\"");
	level_ai_chr_unset_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_unset_chrflag\"");
	level_ai_if_chr_has_chrflag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_chr_has_chrflag\"");
	level_ai_chr_set_hidden_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_hidden_flag\"");
	level_ai_chr_unset_hidden_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_unset_hidden_flag\"");
	level_ai_if_chr_has_hidden_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_chr_has_hidden_flag\"");
	level_ai_set_obj_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_obj_flag\"");
	level_ai_unset_obj_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unset_obj_flag\"");
	level_ai_if_obj_has_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_obj_has_flag\"");
	level_ai_open_door_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.open_door\"");
	level_ai_close_door_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.close_door\"");
	level_ai_if_door_state_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_door_state\"");
	level_ai_if_object_is_door_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_object_is_door\"");
	level_ai_lock_door_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.lock_door\"");
	level_ai_unlock_door_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unlock_door\"");
	level_ai_if_door_locked_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_door_locked\"");
	level_ai_if_lift_stationary_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_lift_stationary\"");
	level_ai_lift_go_to_stop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.lift_go_to_stop\"");
	level_ai_if_lift_at_stop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_lift_at_stop\"");
	level_ai_activate_lift_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.activate_lift\"");
	level_ai_if_using_lift_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_using_lift\"");
	level_ai_configure_rain_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.configure_rain\"");
	level_ai_configure_snow_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.configure_snow\"");
	level_ai_switch_to_alt_sky_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.switch_to_alt_sky\"");
	level_ai_set_wind_speed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_wind_speed\"");
	level_ai_set_lights_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_lights\"");
	level_ai_set_room_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_room_flag\"");
	level_ai_show_cutscene_chrs_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.show_cutscene_chrs\"");
	level_ai_configure_environment_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.configure_environment\"");
	level_ai_if_distance_to_target2_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_target2_less_than\"");
	level_ai_if_distance_to_target2_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_target2_greater_than\"");
	level_ai_speak_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.speak\"");
	level_ai_play_sound_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_sound\"");
	level_ai_assign_sound_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.assign_sound\"");
	level_ai_audio_mute_channel_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.audio_mute_channel\"");
	level_ai_if_channel_free_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_channel_free\"");
	level_ai_set_object_sound_volume_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_object_sound_volume\"");
	level_ai_set_object_sound_volume_by_distance_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_object_sound_volume_by_distance\"");
	level_ai_set_object_sound_playing_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_object_sound_playing\"");
	level_ai_play_repeating_sound_from_object_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.play_repeating_sound_from_object\"");
	level_ai_play_sound_from_entity_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_sound_from_entity\"");
	level_ai_play_repeating_sound_from_pad_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.play_repeating_sound_from_pad\"");
	level_ai_if_object_sound_volume_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_object_sound_volume_less_than\"");
	level_ai_play_sound_from_prop_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_sound_from_prop\"");
	level_ai_play_temporary_primary_track_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.play_temporary_primary_track\"");
	level_ai_play_x_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_x_track\"");
	level_ai_stop_x_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.stop_x_track\"");
	level_ai_play_track_isolated_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_track_isolated\"");
	level_ai_play_default_tracks_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_default_tracks\"");
	level_ai_play_cutscene_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_cutscene_track\"");
	level_ai_stop_cutscene_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.stop_cutscene_track\"");
	level_ai_play_temporary_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.play_temporary_track\"");
	level_ai_stop_ambient_track_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.stop_ambient_track\"");
	level_ai_chr_draw_weapon_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_draw_weapon\"");
	level_ai_chr_draw_weapon_in_cutscene_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_draw_weapon_in_cutscene\"");
	level_ai_set_player_force_speed_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_player_force_speed\"");
	level_ai_chr_set_invincible_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_invincible\"");
	level_ai_if_player_is_invincible_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_is_invincible\"");
	level_ai_if_chr_has_no_gun_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_has_no_gun\"");
	level_ai_chr_delete_weapon_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_delete_weapon\"");
	level_ai_if_trigger_shot_list_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_trigger_shot_list\"");
	level_ai_end_level_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.end_level\"");
	level_ai_end_cutscene_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.end_cutscene\"");
	level_ai_warp_jo_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.warp_jo_to_pad\"");
	level_ai_set_camera_animation_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_camera_animation\"");
	level_ai_if_in_cutscene_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_in_cutscene\"");
	level_ai_if_cutscene_button_pressed_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_cutscene_button_pressed\"");
	level_ai_reorient_for_cutscene_stop_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.reorient_for_cutscene_stop\"");
	level_ai_warp_jo_to_tag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.warp_jo_to_tag\"");
	level_ai_revoke_control_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.revoke_control\"");
	level_ai_grant_control_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.grant_control\"");
	level_ai_player_fade_in_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.player_fade_in\"");
	level_ai_players_fade_out_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.players_fade_out\"");
	level_ai_if_colour_fade_complete_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_colour_fade_complete\"");
	level_ai_prepare_warp_orbit_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.prepare_warp_orbit\"");
	level_ai_begin_warp_latch_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.begin_warp_latch\"");
	level_ai_if_warp_latch_complete_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_warp_latch_complete\"");
	level_ai_spawn_chr_at_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.spawn_chr_at_pad\"");
	level_ai_spawn_chr_at_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.spawn_chr_at_chr\"");
	level_ai_try_equip_weapon_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_equip_weapon\"");
	level_ai_try_equip_hat_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_equip_hat\"");
	level_ai_set_obj_image_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_obj_image\"");
	level_ai_object_do_animation_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.object_do_animation\"");
	level_ai_set_door_open_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_door_open\"");
	level_ai_duplicate_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.duplicate_chr\"");
	level_ai_enable_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.enable_chr\"");
	level_ai_disable_chr_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.disable_chr\"");
	level_ai_enable_obj_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.enable_obj\"");
	level_ai_disable_obj_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.disable_obj\"");
	level_ai_chr_move_to_pad_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_move_to_pad\"");
	level_ai_chr_set_team_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_team\"");
	level_ai_damage_chr_by_amount_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.damage_chr_by_amount\"");
	level_ai_do_preset_animation_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.do_preset_animation\"");
	level_ai_if_player_chr_portal_distance_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_chr_portal_distance_less_than\"");
	level_ai_if_chr_reposition_valid_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_chr_reposition_valid\"");
	level_ai_do_gun_command_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.do_gun_command\"");
	level_ai_if_distance_to_gun_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_distance_to_gun_less_than\"");
	level_ai_recover_gun_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.recover_gun\"");
	level_ai_chr_copy_properties_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_copy_properties\"");
	level_ai_player_auto_walk_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.player_auto_walk\"");
	level_ai_if_player_auto_walk_finished_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_auto_walk_finished\"");
	level_ai_if_obj_in_room_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_obj_in_room\"");
	level_ai_if_player_looking_at_object_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_looking_at_object\"");
	level_ai_if_target_is_player_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_target_is_player\"");
	level_ai_chr_kill_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_kill\"");
	level_ai_remove_weapon_from_inventory_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.remove_weapon_from_inventory\"");
	level_ai_clear_inventory_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.clear_inventory\"");
	level_ai_release_object_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.release_object\"");
	level_ai_chr_grab_object_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_grab_object\"");
	level_ai_toggle_p1p2_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.toggle_p1p2\"");
	level_ai_chr_set_p1p2_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_p1p2\"");
	level_ai_chr_set_cloaked_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_set_cloaked\"");
	level_ai_set_autogun_target_team_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_autogun_target_team\"");
	level_ai_if_objective_complete_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_objective_complete\"");
	level_ai_if_objective_failed_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_objective_failed\"");
	level_ai_if_all_objectives_complete_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_all_objectives_complete\"");
	level_ai_if_difficulty_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_difficulty_less_than\"");
	level_ai_if_difficulty_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_difficulty_greater_than\"");
	level_ai_if_stage_timer_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_stage_timer_less_than\"");
	level_ai_if_stage_timer_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_stage_timer_greater_than\"");
	level_ai_if_stage_id_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_stage_id_less_than\"");
	level_ai_if_stage_id_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_stage_id_greater_than\"");
	level_ai_if_waypoint_within_quadrant_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_waypoint_within_quadrant\"");
	level_ai_set_pad_preset_to_target_quadrant_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_pad_preset_to_target_quadrant\"");
	level_ai_if_num_players_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_players_less_than\"");
	level_ai_if_kill_count_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_kill_count_greater_than\"");
	level_ai_if_num_knocked_out_chrs_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_num_knocked_out_chrs\"");
	level_ai_kill_bond_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.kill_bond\"");
	level_ai_if_pouncebits_eq_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_pouncebits_eq\"");
	level_ai_if_training_pc_holographed_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_training_pc_holographed\"");
	level_ai_if_player_using_device_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_player_using_device\"");
	level_ai_chr_begin_or_end_teleport_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_begin_or_end_teleport\"");
	level_ai_if_chr_teleport_full_white_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_teleport_full_white\"");
	level_ai_chr_set_cutscene_weapon_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_set_cutscene_weapon\"");
	level_ai_fade_screen_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.fade_screen\"");
	level_ai_if_fade_complete_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_fade_complete\"");
	level_ai_set_chr_hudpiece_visible_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_chr_hudpiece_visible\"");
	level_ai_set_passive_mode_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_passive_mode\"");
	level_ai_chr_set_firing_in_cutscene_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.chr_set_firing_in_cutscene\"");
	level_ai_set_portal_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_portal_flag\"");
	level_ai_if_music_event_queue_is_empty_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_music_event_queue_is_empty\"");
	level_ai_if_coop_mode_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.condition.if_coop_mode\"");
	level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\"");
	level_ai_remove_references_to_chr_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.remove_references_to_chr\"");
	level_ai_chr_toggle_model_part_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_toggle_model_part\"");
	level_ai_obj_set_model_part_visible_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.obj_set_model_part_visible\"");
	level_ai_if_obj_health_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.if_obj_health_less_than\"");
	level_ai_set_obj_health_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_obj_health\"");
	level_ai_set_chr_special_death_animation_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_chr_special_death_animation\"");
	level_ai_set_room_to_search_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_room_to_search\"");
	level_ai_restart_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.restart_timer\"");
	level_ai_reset_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.reset_timer\"");
	level_ai_pause_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.pause_timer\"");
	level_ai_resume_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.resume_timer\"");
	level_ai_if_timer_stopped_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_timer_stopped\"");
	level_ai_if_timer_greater_than_random_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.if_timer_greater_than_random\"");
	level_ai_if_timer_less_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_timer_less_than\"");
	level_ai_if_timer_greater_than_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_timer_greater_than\"");
	level_ai_show_countdown_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.show_countdown_timer\"");
	level_ai_hide_countdown_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.hide_countdown_timer\"");
	level_ai_set_countdown_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_countdown_timer\"");
	level_ai_stop_countdown_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.stop_countdown_timer\"");
	level_ai_start_countdown_timer_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.start_countdown_timer\"");
	level_ai_if_countdown_timer_stopped_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.if_countdown_timer_stopped\"");
	level_ai_if_countdown_timer_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.if_countdown_timer_less_than\"");
	level_ai_if_countdown_timer_greater_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.if_countdown_timer_greater_than\"");
	level_ai_set_savefile_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_savefile_flag\"");
	level_ai_unset_savefile_flag_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.unset_savefile_flag\"");
	level_ai_if_savefile_flag_set_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_savefile_flag_set\"");
	level_ai_if_savefile_flag_unset_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.if_savefile_flag_unset\"");
	level_ai_show_hudmsg_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.show_hudmsg\"");
	level_ai_show_hudmsg_middle_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.show_hudmsg_middle\"");
	level_ai_show_hudmsg_top_middle_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.show_hudmsg_top_middle\"");
	level_ai_hovercar_begin_path_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.hovercar_begin_path\"");
	level_ai_set_vehicle_speed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_vehicle_speed\"");
	level_ai_set_rotor_speed_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_rotor_speed\"");
	level_ai_chr_explosions_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_explosions\"");
	level_ai_set_tinted_glass_enabled_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_tinted_glass_enabled\"");
	level_ai_hovercopter_fire_rocket_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.hovercopter_fire_rocket\"");
	level_ai_chr_adjust_motion_blur_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_adjust_motion_blur\"");
	level_ai_punch_or_kick_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.punch_or_kick\"");
	level_ai_set_target_to_eyespy_if_in_sight_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.action.set_target_to_eyespy_if_in_sight\"");
	level_ai_mini_skedar_try_pounce_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.mini_skedar_try_pounce\"");
	level_ai_if_object_distance_to_pad_less_than_node_count =
		s_countTextOccurrences(text,
			"\"kind\": \"scenario.ai.condition.if_object_distance_to_pad_less_than\"");
	level_ai_avoid_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.avoid\"");
	level_ai_title_init_mode_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.title_init_mode\"");
	level_ai_try_exit_title_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.try_exit_title\"");
	level_ai_chr_emit_sparks_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.chr_emit_sparks\"");
	level_ai_set_dr_caroll_images_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.set_dr_caroll_images\"");
	level_ai_say_quip_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.say_quip\"");
	level_ai_say_ci_staff_quip_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.say_ci_staff_quip\"");
	level_ai_shuffle_ruins_pillars_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.shuffle_ruins_pillars\"");
	level_ai_shuffle_pelagic_switches_node_count = s_countTextOccurrences(text,
		"\"kind\": \"scenario.ai.action.shuffle_pelagic_switches\"");
	if (level_global_settings_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable global settings source node");
		free(text);
		return 0;
	}
	if (level_ai_list_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI list source node");
		free(text);
		return 0;
	}
	if (level_ai_stop_node_count != 1 ||
			level_ai_kneel_node_count != 1 ||
			level_ai_surrender_node_count != 1 ||
			level_ai_fade_out_node_count != 1 ||
			level_ai_remove_chr_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI basic/lifecycle action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_try_sidestep_node_count != 1 ||
			level_ai_try_jump_out_node_count != 1 ||
			level_ai_try_run_sideways_node_count != 1 ||
			level_ai_try_attack_walk_node_count != 1 ||
			level_ai_try_attack_run_node_count != 1 ||
			level_ai_try_attack_roll_node_count != 1 ||
			level_ai_try_attack_stand_node_count != 1 ||
			level_ai_try_attack_kneel_node_count != 1 ||
			level_ai_try_attack_lie_node_count != 1 ||
			level_ai_if_attack_locked_node_count != 1 ||
			level_ai_if_attacking_node_count != 1 ||
			level_ai_try_modify_attack_node_count != 1 ||
			level_ai_face_entity_node_count != 1 ||
			level_ai_apply_gset_damage_node_count != 1 ||
			level_ai_chr_damage_chr_node_count != 1 ||
			level_ai_consider_grenade_throw_node_count != 1 ||
			level_ai_drop_item_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI combat/interaction action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_try_run_from_target_node_count != 1 ||
			level_ai_try_jog_to_target_prop_node_count != 1 ||
			level_ai_try_walk_to_target_prop_node_count != 1 ||
			level_ai_try_run_to_target_prop_node_count != 1 ||
			level_ai_try_go_to_cover_prop_node_count != 1 ||
			level_ai_try_jog_to_chr_node_count != 1 ||
			level_ai_try_walk_to_chr_node_count != 1 ||
			level_ai_try_run_to_chr_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI target movement action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_can_hear_alarm_node_count != 1 ||
			level_ai_if_patrolling_node_count != 1 ||
			level_ai_if_alarm_active_node_count != 1 ||
			level_ai_if_gas_active_node_count != 1 ||
			level_ai_if_hears_target_node_count != 1 ||
			level_ai_if_saw_injury_node_count != 1 ||
			level_ai_if_saw_death_node_count != 1 ||
			level_ai_if_los_to_target_node_count != 1 ||
			level_ai_if_los_to_attack_target_node_count != 1 ||
			level_ai_if_target_nearly_in_sight_node_count != 1 ||
			level_ai_if_nearly_in_targets_sight_node_count != 1 ||
			level_ai_set_pad_preset_to_pad_on_route_to_target_node_count != 1 ||
			level_ai_if_saw_target_recently_node_count != 1 ||
			level_ai_if_heard_target_recently_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI perception/alarm action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_los_to_chr_node_count != 1 ||
			level_ai_if_never_been_on_screen_node_count != 1 ||
			level_ai_if_on_screen_node_count != 1 ||
			level_ai_if_chr_in_on_screen_room_node_count != 1 ||
			level_ai_if_room_is_on_screen_node_count != 1 ||
			level_ai_if_target_aiming_at_me_node_count != 1 ||
			level_ai_if_near_miss_node_count != 1 ||
			level_ai_if_sees_suspicious_item_node_count != 1 ||
			level_ai_if_check_fov_with_target_node_count != 1 ||
			level_ai_if_target_in_fov_left_node_count != 1 ||
			level_ai_if_target_out_of_fov_left_node_count != 1 ||
			level_ai_if_target_in_fov_node_count != 1 ||
			level_ai_if_target_out_of_fov_node_count != 1 ||
			level_ai_if_distance_to_target_less_than_node_count != 1 ||
			level_ai_if_distance_to_target_greater_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI spatial perception source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_chr_distance_to_pad_less_than_node_count != 1 ||
			level_ai_if_chr_distance_to_pad_greater_than_node_count != 1 ||
			level_ai_if_distance_to_chr_less_than_node_count != 1 ||
			level_ai_if_distance_to_chr_greater_than_node_count != 1 ||
			level_ai_if_any_chr_near_self_node_count != 1 ||
			level_ai_if_distance_from_target_to_pad_less_than_node_count != 1 ||
			level_ai_if_distance_from_target_to_pad_greater_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI distance perception source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_chr_in_room_node_count != 1 ||
			level_ai_if_target_in_room_node_count != 1 ||
			level_ai_if_chr_has_object_node_count != 1 ||
			level_ai_if_weapon_thrown_node_count != 1 ||
			level_ai_if_weapon_thrown_on_object_node_count != 1 ||
			level_ai_if_chr_has_weapon_equipped_node_count != 1 ||
			level_ai_if_gun_unclaimed_node_count != 1 ||
			level_ai_if_object_healthy_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI room/object/weapon condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_chr_activated_object_node_count != 1 ||
			level_ai_obj_interact_node_count != 1 ||
			level_ai_destroy_object_node_count != 1 ||
			level_ai_drop_object_from_chr_node_count != 1 ||
			level_ai_chr_drop_items_node_count != 1 ||
			level_ai_chr_drop_weapon_node_count != 1 ||
			level_ai_give_object_to_chr_node_count != 1 ||
			level_ai_object_move_to_pad_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI object interaction action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_do_animation_node_count != 1 ||
			level_ai_be_surprised_one_hand_node_count != 1 ||
			level_ai_be_surprised_look_around_node_count != 1 ||
			level_ai_be_surprised_surrender_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI animation action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_random_node_count != 1 ||
			level_ai_if_random_less_than_node_count != 1 ||
			level_ai_if_random_greater_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI random control source nodes");
		free(text);
		return 0;
	}
	if (level_ai_print_node_count != 1 ||
			level_ai_noop_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI debug/no-op action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_list_node_count != 1 ||
			level_ai_set_return_list_node_count != 1 ||
			level_ai_set_shot_list_node_count != 1 ||
			level_ai_return_list_node_count != 1 ||
			level_ai_set_punch_dodge_list_node_count != 1 ||
			level_ai_set_shooting_at_me_list_node_count != 1 ||
			level_ai_set_dark_room_list_node_count != 1 ||
			level_ai_set_player_dead_list_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI list-control action source nodes");
		free(text);
		return 0;
	}
	if (level_pad_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable pad source node");
		free(text);
		return 0;
	}
	if (level_path_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable navigation path source node");
		free(text);
		return 0;
	}
	if (level_ai_jog_to_pad_node_count != 1 ||
			level_ai_goto_pad_preset_node_count != 1 ||
			level_ai_walk_to_pad_node_count != 1 ||
			level_ai_run_to_pad_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI pad action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_path_node_count != 1 ||
			level_ai_start_patrol_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI path action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_pad_preset_node_count != 1 ||
			level_ai_chr_set_pad_preset_node_count != 1 ||
			level_ai_chr_copy_pad_preset_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI pad-preset action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_chr_preset_node_count != 1 ||
			level_ai_set_chr_target_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI chr-preset action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_morale_node_count != 1 ||
			level_ai_add_morale_node_count != 1 ||
			level_ai_chr_add_morale_node_count != 1 ||
			level_ai_subtract_morale_node_count != 1 ||
			level_ai_set_alertness_node_count != 1 ||
			level_ai_add_alertness_node_count != 1 ||
			level_ai_chr_add_alertness_node_count != 1 ||
			level_ai_subtract_alertness_node_count != 1 ||
			level_ai_increase_squadron_alertness_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI morale/alertness action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_num_arghs_less_than_node_count != 1 ||
			level_ai_if_num_arghs_greater_than_node_count != 1 ||
			level_ai_if_num_close_arghs_less_than_node_count != 1 ||
			level_ai_if_num_close_arghs_greater_than_node_count != 1 ||
			level_ai_if_chr_health_greater_than_node_count != 1 ||
			level_ai_if_chr_health_less_than_node_count != 1 ||
			level_ai_if_chr_shield_less_than_node_count != 1 ||
			level_ai_if_chr_shield_greater_than_node_count != 1 ||
			level_ai_if_injured_node_count != 1 ||
			level_ai_if_shield_damaged_node_count != 1 ||
			level_ai_if_morale_less_than_node_count != 1 ||
			level_ai_if_morale_less_than_random_node_count != 1 ||
			level_ai_if_alertness_node_count != 1 ||
			level_ai_if_chr_alertness_less_than_node_count != 1 ||
			level_ai_if_alertness_less_than_random_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI character condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_idle_node_count != 1 ||
			level_ai_if_stopped_node_count != 1 ||
			level_ai_if_chr_dead_node_count != 1 ||
			level_ai_if_chr_death_animation_finished_node_count != 1 ||
			level_ai_if_chr_knocked_out_node_count != 1 ||
			level_ai_if_can_see_target_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI lifecycle/perception condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_hear_distance_node_count != 1 ||
			level_ai_set_view_distance_node_count != 1 ||
			level_ai_set_grenade_probability_node_count != 1 ||
			level_ai_set_chr_num_node_count != 1 ||
			level_ai_set_max_damage_node_count != 1 ||
			level_ai_add_health_node_count != 1 ||
			level_ai_set_shield_node_count != 1 ||
			level_ai_set_reaction_speed_node_count != 1 ||
			level_ai_set_recovery_speed_node_count != 1 ||
			level_ai_set_accuracy_node_count != 1 ||
			level_ai_set_dodge_rating_node_count != 1 ||
			level_ai_set_unarmed_dodge_rating_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI tuning/stat action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_action_node_count != 1 ||
			level_ai_set_team_orders_node_count != 1 ||
			level_ai_retreat_node_count != 1 ||
			level_ai_find_cover_node_count != 1 ||
			level_ai_find_cover_within_dist_node_count != 1 ||
			level_ai_find_cover_outside_dist_node_count != 1 ||
			level_ai_go_to_cover_node_count != 1 ||
			level_ai_check_cover_out_of_sight_node_count != 1 ||
			level_ai_orbit_target_node_count != 1 ||
			level_ai_set_chr_preset_to_unalerted_teammate_node_count != 1 ||
			level_ai_set_squadron_node_count != 1 ||
			level_ai_face_cover_node_count != 1 ||
			level_ai_danger_cover_node_count != 1 ||
			level_ai_release_cover_node_count != 1 ||
			level_ai_rebuild_teams_node_count != 1 ||
			level_ai_rebuild_squadrons_node_count != 1 ||
			level_ai_chr_set_listening_node_count != 1 ||
			level_ai_try_attack_amount_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI action/order/team/cover state nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_chr_not_talking_node_count != 1 ||
			level_ai_if_orders_node_count != 1 ||
			level_ai_if_has_orders_node_count != 1 ||
			level_ai_if_chr_in_squadron_doing_action_node_count != 1 ||
			level_ai_if_chr_listening_node_count != 1 ||
			level_ai_if_not_listening_node_count != 1 ||
			level_ai_if_chr_injured_target_node_count != 1 ||
			level_ai_if_action_node_count != 1 ||
			level_ai_if_chr_ammo_quantity_less_than_node_count != 1 ||
			level_ai_if_chr_target_node_count != 1 ||
			level_ai_if_compare_chr_presets_team_node_count != 1 ||
			level_ai_if_human_node_count != 1 ||
			level_ai_if_skedar_node_count != 1 ||
			level_ai_if_prop_preset_blocking_sight_to_target_node_count != 1 ||
			level_ai_remove_object_at_prop_preset_node_count != 1 ||
			level_ai_if_prop_preset_height_less_than_node_count != 1 ||
			level_ai_set_target_node_count != 1 ||
			level_ai_if_presets_target_is_not_my_target_node_count != 1 ||
			level_ai_set_chr_preset_to_chr_near_self_node_count != 1 ||
			level_ai_set_chr_preset_to_chr_near_pad_node_count != 1 ||
			level_ai_if_dangerous_object_nearby_node_count != 1 ||
			level_ai_if_heli_weapons_armed_node_count != 1 ||
			level_ai_if_hoverbot_next_step_node_count != 1 ||
			level_ai_shuffle_investigation_terminals_node_count != 1 ||
			level_ai_set_pad_preset_to_investigation_terminal_node_count != 1 ||
			level_ai_heli_arm_weapons_node_count != 1 ||
			level_ai_heli_unarm_weapons_node_count != 1 ||
			level_ai_if_safety2_less_than_node_count != 1 ||
			level_ai_if_player_using_cmp_or_ar34_node_count != 1 ||
			level_ai_detect_enemy_on_same_floor_node_count != 1 ||
			level_ai_detect_enemy_node_count != 1 ||
			level_ai_if_safety_less_than_node_count != 1 ||
			level_ai_if_target_moving_slowly_node_count != 1 ||
			level_ai_if_target_moving_closer_node_count != 1 ||
			level_ai_if_target_moving_away_node_count != 1 ||
			level_ai_if_squadron_is_dead_node_count != 1 ||
			level_ai_if_true_node_count != 1 ||
			level_ai_if_num_chrs_in_squadron_greater_than_node_count != 1 ||
			level_ai_if_natural_anim_node_count != 1 ||
			level_ai_if_y_node_count != 1 ||
			level_ai_if_sound_timer_node_count != 1 ||
			level_ai_if_target_y_difference_less_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI intent/status/target/vehicle/safety/misc condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_try_start_alarm_node_count != 1 ||
			level_ai_activate_alarm_node_count != 1 ||
			level_ai_deactivate_alarm_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI alarm action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_flag_node_count != 1 ||
			level_ai_unset_flag_node_count != 1 ||
			level_ai_if_has_flag_node_count != 1 ||
			level_ai_chr_set_flag_node_count != 1 ||
			level_ai_chr_unset_flag_node_count != 1 ||
			level_ai_if_chr_has_flag_node_count != 1 ||
			level_ai_set_stage_flag_node_count != 1 ||
			level_ai_unset_stage_flag_node_count != 1 ||
			level_ai_if_stage_flag_eq_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI flag action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_chrflag_node_count != 1 ||
			level_ai_unset_chrflag_node_count != 1 ||
			level_ai_if_has_chrflag_node_count != 1 ||
			level_ai_chr_set_chrflag_node_count != 1 ||
			level_ai_chr_unset_chrflag_node_count != 1 ||
			level_ai_if_chr_has_chrflag_node_count != 1 ||
			level_ai_chr_set_hidden_flag_node_count != 1 ||
			level_ai_chr_unset_hidden_flag_node_count != 1 ||
			level_ai_if_chr_has_hidden_flag_node_count != 1 ||
			level_ai_set_obj_flag_node_count != 1 ||
			level_ai_unset_obj_flag_node_count != 1 ||
			level_ai_if_obj_has_flag_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI chr/object flag action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_open_door_node_count != 1 ||
			level_ai_close_door_node_count != 1 ||
			level_ai_if_door_state_node_count != 1 ||
			level_ai_if_object_is_door_node_count != 1 ||
			level_ai_lock_door_node_count != 1 ||
			level_ai_unlock_door_node_count != 1 ||
			level_ai_if_door_locked_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI door action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_lift_stationary_node_count != 1 ||
			level_ai_lift_go_to_stop_node_count != 1 ||
			level_ai_if_lift_at_stop_node_count != 1 ||
			level_ai_activate_lift_node_count != 1 ||
			level_ai_if_using_lift_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI lift action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_configure_rain_node_count != 1 ||
			level_ai_configure_snow_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI weather action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_switch_to_alt_sky_node_count != 1 ||
			level_ai_set_wind_speed_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI sky action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_lights_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI lighting action source node");
		free(text);
		return 0;
	}
	if (level_ai_set_room_flag_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI room-flag action source node");
		free(text);
		return 0;
	}
	if (level_ai_show_cutscene_chrs_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI cutscene visibility action source node");
		free(text);
		return 0;
	}
	if (level_ai_configure_environment_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI environment action source node");
		free(text);
		return 0;
	}
	if (level_ai_if_distance_to_target2_less_than_node_count != 1 ||
			level_ai_if_distance_to_target2_greater_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI target-distance condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_speak_node_count != 1 ||
			level_ai_play_sound_node_count != 1 ||
			level_ai_assign_sound_node_count != 1 ||
			level_ai_audio_mute_channel_node_count != 1 ||
			level_ai_if_channel_free_node_count != 1 ||
			level_ai_set_object_sound_volume_node_count != 1 ||
			level_ai_set_object_sound_volume_by_distance_node_count != 1 ||
			level_ai_set_object_sound_playing_node_count != 1 ||
			level_ai_play_repeating_sound_from_object_node_count != 1 ||
			level_ai_play_sound_from_entity_node_count != 1 ||
			level_ai_play_repeating_sound_from_pad_node_count != 1 ||
			level_ai_if_object_sound_volume_less_than_node_count != 1 ||
			level_ai_play_sound_from_prop_node_count != 1 ||
			level_ai_play_temporary_primary_track_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI audio action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_play_x_track_node_count != 1 ||
			level_ai_stop_x_track_node_count != 1 ||
			level_ai_play_track_isolated_node_count != 1 ||
			level_ai_play_default_tracks_node_count != 1 ||
			level_ai_play_cutscene_track_node_count != 1 ||
			level_ai_stop_cutscene_track_node_count != 1 ||
			level_ai_play_temporary_track_node_count != 1 ||
			level_ai_stop_ambient_track_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI music track action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_draw_weapon_node_count != 1 ||
			level_ai_chr_draw_weapon_in_cutscene_node_count != 1 ||
			level_ai_set_player_force_speed_node_count != 1 ||
			level_ai_chr_set_invincible_node_count != 1 ||
			level_ai_if_player_is_invincible_node_count != 1 ||
			level_ai_if_chr_has_no_gun_node_count != 1 ||
			level_ai_chr_delete_weapon_node_count != 1 ||
			level_ai_if_trigger_shot_list_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI player weapon-state source nodes");
		free(text);
		return 0;
	}
	if (level_ai_end_level_node_count != 1 ||
			level_ai_end_cutscene_node_count != 1 ||
			level_ai_warp_jo_to_pad_node_count != 1 ||
			level_ai_set_camera_animation_node_count != 1 ||
			level_ai_if_in_cutscene_node_count != 1 ||
			level_ai_if_cutscene_button_pressed_node_count != 1 ||
			level_ai_reorient_for_cutscene_stop_node_count != 1 ||
			level_ai_warp_jo_to_tag_node_count != 1 ||
			level_ai_revoke_control_node_count != 1 ||
			level_ai_grant_control_node_count != 1 ||
			level_ai_player_fade_in_node_count != 1 ||
			level_ai_players_fade_out_node_count != 1 ||
			level_ai_if_colour_fade_complete_node_count != 1 ||
			level_ai_prepare_warp_orbit_node_count != 1 ||
			level_ai_begin_warp_latch_node_count != 1 ||
			level_ai_if_warp_latch_complete_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI player cutscene/warp source nodes");
		free(text);
		return 0;
	}
	if (level_ai_duplicate_chr_node_count != 1 ||
			level_ai_spawn_chr_at_pad_node_count != 1 ||
			level_ai_spawn_chr_at_chr_node_count != 1 ||
			level_ai_try_equip_weapon_node_count != 1 ||
			level_ai_try_equip_hat_node_count != 1 ||
			level_ai_set_obj_image_node_count != 1 ||
			level_ai_object_do_animation_node_count != 1 ||
			level_ai_set_door_open_node_count != 1 ||
			level_ai_enable_chr_node_count != 1 ||
			level_ai_disable_chr_node_count != 1 ||
			level_ai_enable_obj_node_count != 1 ||
			level_ai_disable_obj_node_count != 1 ||
			level_ai_chr_move_to_pad_node_count != 1 ||
			level_ai_chr_set_team_node_count != 1 ||
			level_ai_damage_chr_by_amount_node_count != 1 ||
			level_ai_do_preset_animation_node_count != 1 ||
			level_ai_if_player_chr_portal_distance_less_than_node_count != 1 ||
			level_ai_if_chr_reposition_valid_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI setup/spawn or entity lifecycle/motion source nodes");
		free(text);
		return 0;
	}
	if (level_ai_do_gun_command_node_count != 1 ||
			level_ai_if_distance_to_gun_less_than_node_count != 1 ||
			level_ai_recover_gun_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI gun interaction source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_copy_properties_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI character property source node");
		free(text);
		return 0;
	}
	if (level_ai_player_auto_walk_node_count != 1 ||
			level_ai_if_player_auto_walk_finished_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI player navigation source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_obj_in_room_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI object-room condition source node");
		free(text);
		return 0;
	}
	if (level_ai_if_player_looking_at_object_node_count != 1 ||
			level_ai_if_target_is_player_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI perception condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_kill_node_count != 1 ||
			level_ai_remove_weapon_from_inventory_node_count != 1 ||
			level_ai_clear_inventory_node_count != 1 ||
			level_ai_release_object_node_count != 1 ||
			level_ai_chr_grab_object_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI character/inventory action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_toggle_p1p2_node_count != 1 ||
			level_ai_chr_set_p1p2_node_count != 1 ||
			level_ai_chr_set_cloaked_node_count != 1 ||
			level_ai_set_autogun_target_team_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI player-state action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_objective_complete_node_count != 1 ||
			level_ai_if_objective_failed_node_count != 1 ||
			level_ai_if_all_objectives_complete_node_count != 1 ||
			level_ai_if_difficulty_less_than_node_count != 1 ||
			level_ai_if_difficulty_greater_than_node_count != 1 ||
			level_ai_if_stage_timer_less_than_node_count != 1 ||
			level_ai_if_stage_timer_greater_than_node_count != 1 ||
			level_ai_if_stage_id_less_than_node_count != 1 ||
			level_ai_if_stage_id_greater_than_node_count != 1 ||
			level_ai_if_num_players_less_than_node_count != 1 ||
			level_ai_if_kill_count_greater_than_node_count != 1 ||
			level_ai_if_num_knocked_out_chrs_node_count != 1 ||
			level_ai_kill_bond_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI mission/global source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_waypoint_within_quadrant_node_count != 1 ||
			level_ai_set_pad_preset_to_target_quadrant_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI quadrant pad-preset source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_pouncebits_eq_node_count != 1 ||
			level_ai_if_training_pc_holographed_node_count != 1 ||
			level_ai_if_player_using_device_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI state/device condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_begin_or_end_teleport_node_count != 1 ||
			level_ai_if_chr_teleport_full_white_node_count != 1 ||
			level_ai_chr_set_cutscene_weapon_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI teleport/cutscene-weapon source nodes");
		free(text);
		return 0;
	}
	if (level_ai_fade_screen_node_count != 1 ||
			level_ai_if_fade_complete_node_count != 1 ||
			level_ai_set_chr_hudpiece_visible_node_count != 1 ||
			level_ai_set_passive_mode_node_count != 1 ||
			level_ai_chr_set_firing_in_cutscene_node_count != 1 ||
			level_ai_set_portal_flag_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI cutscene presentation source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_music_event_queue_is_empty_node_count != 1 ||
			level_ai_if_coop_mode_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI music/mode condition source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count != 1 ||
			level_ai_remove_references_to_chr_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI pad/reference source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_toggle_model_part_node_count != 1 ||
			level_ai_obj_set_model_part_visible_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI model-part action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_if_obj_health_less_than_node_count != 1 ||
			level_ai_set_obj_health_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI object-health action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_set_chr_special_death_animation_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI special-death action source node");
		free(text);
		return 0;
	}
	if (level_ai_set_room_to_search_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI room-search action source node");
		free(text);
		return 0;
	}
	if (level_ai_set_savefile_flag_node_count != 1 ||
			level_ai_unset_savefile_flag_node_count != 1 ||
			level_ai_if_savefile_flag_set_node_count != 1 ||
			level_ai_if_savefile_flag_unset_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI savefile flag action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_restart_timer_node_count != 1 ||
			level_ai_reset_timer_node_count != 1 ||
			level_ai_pause_timer_node_count != 1 ||
			level_ai_resume_timer_node_count != 1 ||
			level_ai_if_timer_stopped_node_count != 1 ||
			level_ai_if_timer_greater_than_random_node_count != 1 ||
			level_ai_if_timer_less_than_node_count != 1 ||
			level_ai_if_timer_greater_than_node_count != 1 ||
			level_ai_show_countdown_timer_node_count != 1 ||
			level_ai_hide_countdown_timer_node_count != 1 ||
			level_ai_set_countdown_timer_node_count != 1 ||
			level_ai_stop_countdown_timer_node_count != 1 ||
			level_ai_start_countdown_timer_node_count != 1 ||
			level_ai_if_countdown_timer_stopped_node_count != 1 ||
			level_ai_if_countdown_timer_less_than_node_count != 1 ||
			level_ai_if_countdown_timer_greater_than_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI timer/countdown action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_show_hudmsg_node_count != 1 ||
			level_ai_show_hudmsg_middle_node_count != 1 ||
			level_ai_show_hudmsg_top_middle_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI HUD message action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_hovercar_begin_path_node_count != 1 ||
			level_ai_set_vehicle_speed_node_count != 1 ||
			level_ai_set_rotor_speed_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI vehicle motion action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_chr_explosions_node_count != 1 ||
			level_ai_set_tinted_glass_enabled_node_count != 1 ||
			level_ai_hovercopter_fire_rocket_node_count != 1 ||
			level_ai_chr_adjust_motion_blur_node_count != 1 ||
			level_ai_punch_or_kick_node_count != 1 ||
			level_ai_set_target_to_eyespy_if_in_sight_node_count != 1 ||
			level_ai_mini_skedar_try_pounce_node_count != 1 ||
			level_ai_if_object_distance_to_pad_less_than_node_count != 1 ||
			level_ai_avoid_node_count != 1 ||
			level_ai_title_init_mode_node_count != 1 ||
			level_ai_try_exit_title_node_count != 1 ||
			level_ai_chr_emit_sparks_node_count != 1 ||
			level_ai_set_dr_caroll_images_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI misc effect action source nodes");
		free(text);
		return 0;
	}
	if (level_ai_say_quip_node_count != 1 ||
			level_ai_say_ci_staff_quip_node_count != 1 ||
			level_ai_shuffle_ruins_pillars_node_count != 1 ||
			level_ai_shuffle_pelagic_switches_node_count != 1) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"missing executable AI quip/setup shuffle action source nodes");
		free(text);
		return 0;
	}
	if (!s_jsonStringValueForKey(text, "scenario",
			s_ActiveScenarioGraphs.level_global_settings_scenario,
			sizeof(s_ActiveScenarioGraphs.level_global_settings_scenario)) ||
			strcmp(s_ActiveScenarioGraphs.level_global_settings_scenario,
				scenario->id) != 0) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"global settings source scenario mismatch");
		free(text);
		return 0;
	}
	s_jsonStringValueForKey(text, "kind",
		s_ActiveScenarioGraphs.level_global_settings_kind,
		sizeof(s_ActiveScenarioGraphs.level_global_settings_kind));

	s_copyString(s_ActiveScenarioGraphs.scenario_id,
		sizeof(s_ActiveScenarioGraphs.scenario_id), scenario->id);
	s_copyString(s_ActiveScenarioGraphs.level_graph_path,
		sizeof(s_ActiveScenarioGraphs.level_graph_path), graph_path);
	if (!s_bindLevelGraphTablePath(scenario, text, graph_path, "pads",
			s_ActiveScenarioGraphs.pads_path,
			sizeof(s_ActiveScenarioGraphs.pads_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "spawns",
			s_ActiveScenarioGraphs.spawns_path,
			sizeof(s_ActiveScenarioGraphs.spawns_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "objects",
			s_ActiveScenarioGraphs.objects_path,
			sizeof(s_ActiveScenarioGraphs.objects_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "setup_fields",
			s_ActiveScenarioGraphs.setup_fields_path,
			sizeof(s_ActiveScenarioGraphs.setup_fields_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "ai_lists",
			s_ActiveScenarioGraphs.ai_lists_path,
			sizeof(s_ActiveScenarioGraphs.ai_lists_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "volumes",
			s_ActiveScenarioGraphs.volumes_path,
			sizeof(s_ActiveScenarioGraphs.volumes_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "objectives",
			s_ActiveScenarioGraphs.objectives_path,
			sizeof(s_ActiveScenarioGraphs.objectives_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "waypoints",
			s_ActiveScenarioGraphs.waypoints_path,
			sizeof(s_ActiveScenarioGraphs.waypoints_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "waygroups",
			s_ActiveScenarioGraphs.waygroups_path,
			sizeof(s_ActiveScenarioGraphs.waygroups_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "covers",
			s_ActiveScenarioGraphs.covers_path,
			sizeof(s_ActiveScenarioGraphs.covers_path)) ||
			!s_bindLevelGraphTablePath(scenario, text, graph_path, "paths",
			s_ActiveScenarioGraphs.paths_path,
			sizeof(s_ActiveScenarioGraphs.paths_path))) {
		s_resetActiveScenarioGraphs();
		free(text);
		return 0;
	}
	level_volume_count = 0;
	if (!s_loadLevelVolumeSourceRows(s_ActiveScenarioGraphs.volumes_path,
			&level_volume_rows, &level_volume_count)) {
		s_graphFailure(ASSET_SCENARIO, scenario->id,
			s_ActiveScenarioGraphs.volumes_path,
			"missing executable level volume source rows");
		s_resetActiveScenarioGraphs();
		free(text);
		return 0;
	}
	if (level_volume_node_count != level_volume_count) {
		s_graphFailure(ASSET_SCENARIO, scenario->id, graph_path,
			"trigger volume node count differs from volumes.tsv rows");
		s_resetActiveScenarioGraphs();
		free(text);
		free(level_volume_rows);
		return 0;
	}
	s_ActiveScenarioGraphs.level_graph_size = text_size;
	s_ActiveScenarioGraphs.level_volumes = level_volume_rows;
	s_ActiveScenarioGraphs.level_volume_count = level_volume_count;
	s_ActiveScenarioGraphs.level_volume_node_count =
		level_volume_node_count;
	s_ActiveScenarioGraphs.level_pad_node_count =
		level_pad_node_count;
	s_ActiveScenarioGraphs.level_global_settings_node_count =
		level_global_settings_node_count;
	s_ActiveScenarioGraphs.level_ai_list_node_count =
		level_ai_list_node_count;
	s_ActiveScenarioGraphs.level_ai_stop_node_count =
		level_ai_stop_node_count;
	s_ActiveScenarioGraphs.level_ai_kneel_node_count =
		level_ai_kneel_node_count;
	s_ActiveScenarioGraphs.level_ai_surrender_node_count =
		level_ai_surrender_node_count;
	s_ActiveScenarioGraphs.level_ai_fade_out_node_count =
		level_ai_fade_out_node_count;
	s_ActiveScenarioGraphs.level_ai_remove_chr_node_count =
		level_ai_remove_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_try_sidestep_node_count =
		level_ai_try_sidestep_node_count;
	s_ActiveScenarioGraphs.level_ai_try_jump_out_node_count =
		level_ai_try_jump_out_node_count;
	s_ActiveScenarioGraphs.level_ai_try_run_sideways_node_count =
		level_ai_try_run_sideways_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_walk_node_count =
		level_ai_try_attack_walk_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_run_node_count =
		level_ai_try_attack_run_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_roll_node_count =
		level_ai_try_attack_roll_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_stand_node_count =
		level_ai_try_attack_stand_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_kneel_node_count =
		level_ai_try_attack_kneel_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_lie_node_count =
		level_ai_try_attack_lie_node_count;
	s_ActiveScenarioGraphs.level_ai_if_attack_locked_node_count =
		level_ai_if_attack_locked_node_count;
	s_ActiveScenarioGraphs.level_ai_if_attacking_node_count =
		level_ai_if_attacking_node_count;
	s_ActiveScenarioGraphs.level_ai_try_modify_attack_node_count =
		level_ai_try_modify_attack_node_count;
	s_ActiveScenarioGraphs.level_ai_face_entity_node_count =
		level_ai_face_entity_node_count;
	s_ActiveScenarioGraphs.level_ai_apply_gset_damage_node_count =
		level_ai_apply_gset_damage_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_damage_chr_node_count =
		level_ai_chr_damage_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_consider_grenade_throw_node_count =
		level_ai_consider_grenade_throw_node_count;
	s_ActiveScenarioGraphs.level_ai_drop_item_node_count =
		level_ai_drop_item_node_count;
	s_ActiveScenarioGraphs.level_ai_try_run_from_target_node_count =
		level_ai_try_run_from_target_node_count;
	s_ActiveScenarioGraphs.level_ai_try_jog_to_target_prop_node_count =
		level_ai_try_jog_to_target_prop_node_count;
	s_ActiveScenarioGraphs.level_ai_try_walk_to_target_prop_node_count =
		level_ai_try_walk_to_target_prop_node_count;
	s_ActiveScenarioGraphs.level_ai_try_run_to_target_prop_node_count =
		level_ai_try_run_to_target_prop_node_count;
	s_ActiveScenarioGraphs.level_ai_try_go_to_cover_prop_node_count =
		level_ai_try_go_to_cover_prop_node_count;
	s_ActiveScenarioGraphs.level_ai_try_jog_to_chr_node_count =
		level_ai_try_jog_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_try_walk_to_chr_node_count =
		level_ai_try_walk_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_try_run_to_chr_node_count =
		level_ai_try_run_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_if_can_hear_alarm_node_count =
		level_ai_if_can_hear_alarm_node_count;
	s_ActiveScenarioGraphs.level_ai_if_patrolling_node_count =
		level_ai_if_patrolling_node_count;
	s_ActiveScenarioGraphs.level_ai_if_alarm_active_node_count =
		level_ai_if_alarm_active_node_count;
	s_ActiveScenarioGraphs.level_ai_if_gas_active_node_count =
		level_ai_if_gas_active_node_count;
	s_ActiveScenarioGraphs.level_ai_if_hears_target_node_count =
		level_ai_if_hears_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_saw_injury_node_count =
		level_ai_if_saw_injury_node_count;
	s_ActiveScenarioGraphs.level_ai_if_saw_death_node_count =
		level_ai_if_saw_death_node_count;
	s_ActiveScenarioGraphs.level_ai_if_los_to_target_node_count =
		level_ai_if_los_to_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_los_to_attack_target_node_count =
		level_ai_if_los_to_attack_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_nearly_in_sight_node_count =
		level_ai_if_target_nearly_in_sight_node_count;
	s_ActiveScenarioGraphs.level_ai_if_nearly_in_targets_sight_node_count =
		level_ai_if_nearly_in_targets_sight_node_count;
	s_ActiveScenarioGraphs.level_ai_set_pad_preset_to_pad_on_route_to_target_node_count =
		level_ai_set_pad_preset_to_pad_on_route_to_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_saw_target_recently_node_count =
		level_ai_if_saw_target_recently_node_count;
	s_ActiveScenarioGraphs.level_ai_if_heard_target_recently_node_count =
		level_ai_if_heard_target_recently_node_count;
	s_ActiveScenarioGraphs.level_ai_if_los_to_chr_node_count =
		level_ai_if_los_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_if_never_been_on_screen_node_count =
		level_ai_if_never_been_on_screen_node_count;
	s_ActiveScenarioGraphs.level_ai_if_on_screen_node_count =
		level_ai_if_on_screen_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_in_on_screen_room_node_count =
		level_ai_if_chr_in_on_screen_room_node_count;
	s_ActiveScenarioGraphs.level_ai_if_room_is_on_screen_node_count =
		level_ai_if_room_is_on_screen_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_aiming_at_me_node_count =
		level_ai_if_target_aiming_at_me_node_count;
	s_ActiveScenarioGraphs.level_ai_if_near_miss_node_count =
		level_ai_if_near_miss_node_count;
	s_ActiveScenarioGraphs.level_ai_if_sees_suspicious_item_node_count =
		level_ai_if_sees_suspicious_item_node_count;
	s_ActiveScenarioGraphs.level_ai_if_check_fov_with_target_node_count =
		level_ai_if_check_fov_with_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_in_fov_left_node_count =
		level_ai_if_target_in_fov_left_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_left_node_count =
		level_ai_if_target_out_of_fov_left_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_in_fov_node_count =
		level_ai_if_target_in_fov_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_node_count =
		level_ai_if_target_out_of_fov_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_target_less_than_node_count =
		level_ai_if_distance_to_target_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_target_greater_than_node_count =
		level_ai_if_distance_to_target_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_distance_to_pad_less_than_node_count =
		level_ai_if_chr_distance_to_pad_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_distance_to_pad_greater_than_node_count =
		level_ai_if_chr_distance_to_pad_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_chr_less_than_node_count =
		level_ai_if_distance_to_chr_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_chr_greater_than_node_count =
		level_ai_if_distance_to_chr_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_any_chr_near_self_node_count =
		level_ai_if_any_chr_near_self_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_from_target_to_pad_less_than_node_count =
		level_ai_if_distance_from_target_to_pad_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_from_target_to_pad_greater_than_node_count =
		level_ai_if_distance_from_target_to_pad_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_in_room_node_count =
		level_ai_if_chr_in_room_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_in_room_node_count =
		level_ai_if_target_in_room_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_object_node_count =
		level_ai_if_chr_has_object_node_count;
	s_ActiveScenarioGraphs.level_ai_if_weapon_thrown_node_count =
		level_ai_if_weapon_thrown_node_count;
	s_ActiveScenarioGraphs.level_ai_if_weapon_thrown_on_object_node_count =
		level_ai_if_weapon_thrown_on_object_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_weapon_equipped_node_count =
		level_ai_if_chr_has_weapon_equipped_node_count;
	s_ActiveScenarioGraphs.level_ai_if_gun_unclaimed_node_count =
		level_ai_if_gun_unclaimed_node_count;
	s_ActiveScenarioGraphs.level_ai_if_object_healthy_node_count =
		level_ai_if_object_healthy_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_activated_object_node_count =
		level_ai_if_chr_activated_object_node_count;
	s_ActiveScenarioGraphs.level_ai_obj_interact_node_count =
		level_ai_obj_interact_node_count;
	s_ActiveScenarioGraphs.level_ai_destroy_object_node_count =
		level_ai_destroy_object_node_count;
	s_ActiveScenarioGraphs.level_ai_drop_object_from_chr_node_count =
		level_ai_drop_object_from_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_drop_items_node_count =
		level_ai_chr_drop_items_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_drop_weapon_node_count =
		level_ai_chr_drop_weapon_node_count;
	s_ActiveScenarioGraphs.level_ai_give_object_to_chr_node_count =
		level_ai_give_object_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_object_move_to_pad_node_count =
		level_ai_object_move_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_do_animation_node_count =
		level_ai_chr_do_animation_node_count;
	s_ActiveScenarioGraphs.level_ai_be_surprised_one_hand_node_count =
		level_ai_be_surprised_one_hand_node_count;
	s_ActiveScenarioGraphs.level_ai_be_surprised_look_around_node_count =
		level_ai_be_surprised_look_around_node_count;
	s_ActiveScenarioGraphs.level_ai_be_surprised_surrender_node_count =
		level_ai_be_surprised_surrender_node_count;
	s_ActiveScenarioGraphs.level_ai_random_node_count =
		level_ai_random_node_count;
	s_ActiveScenarioGraphs.level_ai_if_random_less_than_node_count =
		level_ai_if_random_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_random_greater_than_node_count =
		level_ai_if_random_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_print_node_count =
		level_ai_print_node_count;
	s_ActiveScenarioGraphs.level_ai_noop_node_count =
		level_ai_noop_node_count;
	s_ActiveScenarioGraphs.level_ai_set_list_node_count =
		level_ai_set_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_return_list_node_count =
		level_ai_set_return_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_shot_list_node_count =
		level_ai_set_shot_list_node_count;
	s_ActiveScenarioGraphs.level_ai_return_list_node_count =
		level_ai_return_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_punch_dodge_list_node_count =
		level_ai_set_punch_dodge_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_shooting_at_me_list_node_count =
		level_ai_set_shooting_at_me_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_dark_room_list_node_count =
		level_ai_set_dark_room_list_node_count;
	s_ActiveScenarioGraphs.level_ai_set_player_dead_list_node_count =
		level_ai_set_player_dead_list_node_count;
	s_ActiveScenarioGraphs.level_path_node_count =
		level_path_node_count;
	s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count =
		level_ai_jog_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count =
		level_ai_goto_pad_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count =
		level_ai_walk_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count =
		level_ai_run_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_set_path_node_count =
		level_ai_set_path_node_count;
	s_ActiveScenarioGraphs.level_ai_start_patrol_node_count =
		level_ai_start_patrol_node_count;
	s_ActiveScenarioGraphs.level_ai_set_pad_preset_node_count =
		level_ai_set_pad_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_pad_preset_node_count =
		level_ai_chr_set_pad_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_copy_pad_preset_node_count =
		level_ai_chr_copy_pad_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_preset_node_count =
		level_ai_set_chr_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_target_node_count =
		level_ai_set_chr_target_node_count;
	s_ActiveScenarioGraphs.level_ai_set_morale_node_count =
		level_ai_set_morale_node_count;
	s_ActiveScenarioGraphs.level_ai_add_morale_node_count =
		level_ai_add_morale_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_add_morale_node_count =
		level_ai_chr_add_morale_node_count;
	s_ActiveScenarioGraphs.level_ai_subtract_morale_node_count =
		level_ai_subtract_morale_node_count;
	s_ActiveScenarioGraphs.level_ai_set_alertness_node_count =
		level_ai_set_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_add_alertness_node_count =
		level_ai_add_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_add_alertness_node_count =
		level_ai_chr_add_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_subtract_alertness_node_count =
		level_ai_subtract_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_arghs_less_than_node_count =
		level_ai_if_num_arghs_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_arghs_greater_than_node_count =
		level_ai_if_num_arghs_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_close_arghs_less_than_node_count =
		level_ai_if_num_close_arghs_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_close_arghs_greater_than_node_count =
		level_ai_if_num_close_arghs_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_health_greater_than_node_count =
		level_ai_if_chr_health_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_health_less_than_node_count =
		level_ai_if_chr_health_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_shield_less_than_node_count =
		level_ai_if_chr_shield_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_shield_greater_than_node_count =
		level_ai_if_chr_shield_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_injured_node_count =
		level_ai_if_injured_node_count;
	s_ActiveScenarioGraphs.level_ai_if_shield_damaged_node_count =
		level_ai_if_shield_damaged_node_count;
	s_ActiveScenarioGraphs.level_ai_if_morale_less_than_node_count =
		level_ai_if_morale_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_morale_less_than_random_node_count =
		level_ai_if_morale_less_than_random_node_count;
	s_ActiveScenarioGraphs.level_ai_if_alertness_node_count =
		level_ai_if_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_alertness_less_than_node_count =
		level_ai_if_chr_alertness_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_alertness_less_than_random_node_count =
		level_ai_if_alertness_less_than_random_node_count;
	s_ActiveScenarioGraphs.level_ai_if_idle_node_count =
		level_ai_if_idle_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stopped_node_count =
		level_ai_if_stopped_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_dead_node_count =
		level_ai_if_chr_dead_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_death_animation_finished_node_count =
		level_ai_if_chr_death_animation_finished_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_knocked_out_node_count =
		level_ai_if_chr_knocked_out_node_count;
	s_ActiveScenarioGraphs.level_ai_if_can_see_target_node_count =
		level_ai_if_can_see_target_node_count;
	s_ActiveScenarioGraphs.level_ai_increase_squadron_alertness_node_count =
		level_ai_increase_squadron_alertness_node_count;
	s_ActiveScenarioGraphs.level_ai_set_hear_distance_node_count =
		level_ai_set_hear_distance_node_count;
	s_ActiveScenarioGraphs.level_ai_set_view_distance_node_count =
		level_ai_set_view_distance_node_count;
	s_ActiveScenarioGraphs.level_ai_set_grenade_probability_node_count =
		level_ai_set_grenade_probability_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_num_node_count =
		level_ai_set_chr_num_node_count;
	s_ActiveScenarioGraphs.level_ai_set_max_damage_node_count =
		level_ai_set_max_damage_node_count;
	s_ActiveScenarioGraphs.level_ai_add_health_node_count =
		level_ai_add_health_node_count;
	s_ActiveScenarioGraphs.level_ai_set_shield_node_count =
		level_ai_set_shield_node_count;
	s_ActiveScenarioGraphs.level_ai_set_reaction_speed_node_count =
		level_ai_set_reaction_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_set_recovery_speed_node_count =
		level_ai_set_recovery_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_set_accuracy_node_count =
		level_ai_set_accuracy_node_count;
	s_ActiveScenarioGraphs.level_ai_set_dodge_rating_node_count =
		level_ai_set_dodge_rating_node_count;
	s_ActiveScenarioGraphs.level_ai_set_unarmed_dodge_rating_node_count =
		level_ai_set_unarmed_dodge_rating_node_count;
	s_ActiveScenarioGraphs.level_ai_set_action_node_count =
		level_ai_set_action_node_count;
	s_ActiveScenarioGraphs.level_ai_set_team_orders_node_count =
		level_ai_set_team_orders_node_count;
	s_ActiveScenarioGraphs.level_ai_retreat_node_count =
		level_ai_retreat_node_count;
	s_ActiveScenarioGraphs.level_ai_find_cover_node_count =
		level_ai_find_cover_node_count;
	s_ActiveScenarioGraphs.level_ai_find_cover_within_dist_node_count =
		level_ai_find_cover_within_dist_node_count;
	s_ActiveScenarioGraphs.level_ai_find_cover_outside_dist_node_count =
		level_ai_find_cover_outside_dist_node_count;
	s_ActiveScenarioGraphs.level_ai_go_to_cover_node_count =
		level_ai_go_to_cover_node_count;
	s_ActiveScenarioGraphs.level_ai_check_cover_out_of_sight_node_count =
		level_ai_check_cover_out_of_sight_node_count;
	s_ActiveScenarioGraphs.level_ai_orbit_target_node_count =
		level_ai_orbit_target_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_preset_to_unalerted_teammate_node_count =
		level_ai_set_chr_preset_to_unalerted_teammate_node_count;
	s_ActiveScenarioGraphs.level_ai_set_squadron_node_count =
		level_ai_set_squadron_node_count;
	s_ActiveScenarioGraphs.level_ai_face_cover_node_count =
		level_ai_face_cover_node_count;
	s_ActiveScenarioGraphs.level_ai_danger_cover_node_count =
		level_ai_danger_cover_node_count;
	s_ActiveScenarioGraphs.level_ai_release_cover_node_count =
		level_ai_release_cover_node_count;
	s_ActiveScenarioGraphs.level_ai_rebuild_teams_node_count =
		level_ai_rebuild_teams_node_count;
	s_ActiveScenarioGraphs.level_ai_rebuild_squadrons_node_count =
		level_ai_rebuild_squadrons_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_listening_node_count =
		level_ai_chr_set_listening_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_not_talking_node_count =
		level_ai_if_chr_not_talking_node_count;
	s_ActiveScenarioGraphs.level_ai_if_orders_node_count =
		level_ai_if_orders_node_count;
	s_ActiveScenarioGraphs.level_ai_if_has_orders_node_count =
		level_ai_if_has_orders_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_in_squadron_doing_action_node_count =
		level_ai_if_chr_in_squadron_doing_action_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_listening_node_count =
		level_ai_if_chr_listening_node_count;
	s_ActiveScenarioGraphs.level_ai_if_not_listening_node_count =
		level_ai_if_not_listening_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_injured_target_node_count =
		level_ai_if_chr_injured_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_action_node_count =
		level_ai_if_action_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_ammo_quantity_less_than_node_count =
		level_ai_if_chr_ammo_quantity_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_target_node_count =
		level_ai_if_chr_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_compare_chr_presets_team_node_count =
		level_ai_if_compare_chr_presets_team_node_count;
	s_ActiveScenarioGraphs.level_ai_if_human_node_count =
		level_ai_if_human_node_count;
	s_ActiveScenarioGraphs.level_ai_if_skedar_node_count =
		level_ai_if_skedar_node_count;
	s_ActiveScenarioGraphs.level_ai_if_prop_preset_blocking_sight_to_target_node_count =
		level_ai_if_prop_preset_blocking_sight_to_target_node_count;
	s_ActiveScenarioGraphs.level_ai_remove_object_at_prop_preset_node_count =
		level_ai_remove_object_at_prop_preset_node_count;
	s_ActiveScenarioGraphs.level_ai_if_prop_preset_height_less_than_node_count =
		level_ai_if_prop_preset_height_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_set_target_node_count =
		level_ai_set_target_node_count;
	s_ActiveScenarioGraphs.level_ai_if_presets_target_is_not_my_target_node_count =
		level_ai_if_presets_target_is_not_my_target_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_preset_to_chr_near_self_node_count =
		level_ai_set_chr_preset_to_chr_near_self_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_preset_to_chr_near_pad_node_count =
		level_ai_set_chr_preset_to_chr_near_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_if_dangerous_object_nearby_node_count =
		level_ai_if_dangerous_object_nearby_node_count;
	s_ActiveScenarioGraphs.level_ai_if_heli_weapons_armed_node_count =
		level_ai_if_heli_weapons_armed_node_count;
	s_ActiveScenarioGraphs.level_ai_if_hoverbot_next_step_node_count =
		level_ai_if_hoverbot_next_step_node_count;
	s_ActiveScenarioGraphs.level_ai_shuffle_investigation_terminals_node_count =
		level_ai_shuffle_investigation_terminals_node_count;
	s_ActiveScenarioGraphs.level_ai_set_pad_preset_to_investigation_terminal_node_count =
		level_ai_set_pad_preset_to_investigation_terminal_node_count;
	s_ActiveScenarioGraphs.level_ai_heli_arm_weapons_node_count =
		level_ai_heli_arm_weapons_node_count;
	s_ActiveScenarioGraphs.level_ai_heli_unarm_weapons_node_count =
		level_ai_heli_unarm_weapons_node_count;
	s_ActiveScenarioGraphs.level_ai_if_safety2_less_than_node_count =
		level_ai_if_safety2_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_using_cmp_or_ar34_node_count =
		level_ai_if_player_using_cmp_or_ar34_node_count;
	s_ActiveScenarioGraphs.level_ai_detect_enemy_on_same_floor_node_count =
		level_ai_detect_enemy_on_same_floor_node_count;
	s_ActiveScenarioGraphs.level_ai_detect_enemy_node_count =
		level_ai_detect_enemy_node_count;
	s_ActiveScenarioGraphs.level_ai_if_safety_less_than_node_count =
		level_ai_if_safety_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_moving_slowly_node_count =
		level_ai_if_target_moving_slowly_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_moving_closer_node_count =
		level_ai_if_target_moving_closer_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_moving_away_node_count =
		level_ai_if_target_moving_away_node_count;
	s_ActiveScenarioGraphs.level_ai_if_squadron_is_dead_node_count =
		level_ai_if_squadron_is_dead_node_count;
	s_ActiveScenarioGraphs.level_ai_if_true_node_count =
		level_ai_if_true_node_count;
	s_ActiveScenarioGraphs
		.level_ai_if_num_chrs_in_squadron_greater_than_node_count =
		level_ai_if_num_chrs_in_squadron_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_natural_anim_node_count =
		level_ai_if_natural_anim_node_count;
	s_ActiveScenarioGraphs.level_ai_if_y_node_count =
		level_ai_if_y_node_count;
	s_ActiveScenarioGraphs.level_ai_if_sound_timer_node_count =
		level_ai_if_sound_timer_node_count;
	s_ActiveScenarioGraphs
		.level_ai_if_target_y_difference_less_than_node_count =
		level_ai_if_target_y_difference_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_try_attack_amount_node_count =
		level_ai_try_attack_amount_node_count;
	s_ActiveScenarioGraphs.level_ai_try_start_alarm_node_count =
		level_ai_try_start_alarm_node_count;
	s_ActiveScenarioGraphs.level_ai_activate_alarm_node_count =
		level_ai_activate_alarm_node_count;
	s_ActiveScenarioGraphs.level_ai_deactivate_alarm_node_count =
		level_ai_deactivate_alarm_node_count;
	s_ActiveScenarioGraphs.level_ai_set_flag_node_count =
		level_ai_set_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_unset_flag_node_count =
		level_ai_unset_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_has_flag_node_count =
		level_ai_if_has_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_flag_node_count =
		level_ai_chr_set_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_unset_flag_node_count =
		level_ai_chr_unset_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_flag_node_count =
		level_ai_if_chr_has_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_set_stage_flag_node_count =
		level_ai_set_stage_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_unset_stage_flag_node_count =
		level_ai_unset_stage_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stage_flag_eq_node_count =
		level_ai_if_stage_flag_eq_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chrflag_node_count =
		level_ai_set_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_unset_chrflag_node_count =
		level_ai_unset_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_has_chrflag_node_count =
		level_ai_if_has_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_chrflag_node_count =
		level_ai_chr_set_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_unset_chrflag_node_count =
		level_ai_chr_unset_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_chrflag_node_count =
		level_ai_if_chr_has_chrflag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_hidden_flag_node_count =
		level_ai_chr_set_hidden_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_unset_hidden_flag_node_count =
		level_ai_chr_unset_hidden_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_hidden_flag_node_count =
		level_ai_if_chr_has_hidden_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_set_obj_flag_node_count =
		level_ai_set_obj_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_unset_obj_flag_node_count =
		level_ai_unset_obj_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_obj_has_flag_node_count =
		level_ai_if_obj_has_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_open_door_node_count =
		level_ai_open_door_node_count;
	s_ActiveScenarioGraphs.level_ai_close_door_node_count =
		level_ai_close_door_node_count;
	s_ActiveScenarioGraphs.level_ai_if_door_state_node_count =
		level_ai_if_door_state_node_count;
	s_ActiveScenarioGraphs.level_ai_if_object_is_door_node_count =
		level_ai_if_object_is_door_node_count;
	s_ActiveScenarioGraphs.level_ai_lock_door_node_count =
		level_ai_lock_door_node_count;
	s_ActiveScenarioGraphs.level_ai_unlock_door_node_count =
		level_ai_unlock_door_node_count;
	s_ActiveScenarioGraphs.level_ai_if_door_locked_node_count =
		level_ai_if_door_locked_node_count;
	s_ActiveScenarioGraphs.level_ai_if_lift_stationary_node_count =
		level_ai_if_lift_stationary_node_count;
	s_ActiveScenarioGraphs.level_ai_lift_go_to_stop_node_count =
		level_ai_lift_go_to_stop_node_count;
	s_ActiveScenarioGraphs.level_ai_if_lift_at_stop_node_count =
		level_ai_if_lift_at_stop_node_count;
	s_ActiveScenarioGraphs.level_ai_activate_lift_node_count =
		level_ai_activate_lift_node_count;
	s_ActiveScenarioGraphs.level_ai_if_using_lift_node_count =
		level_ai_if_using_lift_node_count;
	s_ActiveScenarioGraphs.level_ai_configure_rain_node_count =
		level_ai_configure_rain_node_count;
	s_ActiveScenarioGraphs.level_ai_configure_snow_node_count =
		level_ai_configure_snow_node_count;
	s_ActiveScenarioGraphs.level_ai_switch_to_alt_sky_node_count =
		level_ai_switch_to_alt_sky_node_count;
	s_ActiveScenarioGraphs.level_ai_set_wind_speed_node_count =
		level_ai_set_wind_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_set_lights_node_count =
		level_ai_set_lights_node_count;
	s_ActiveScenarioGraphs.level_ai_set_room_flag_node_count =
		level_ai_set_room_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_show_cutscene_chrs_node_count =
		level_ai_show_cutscene_chrs_node_count;
	s_ActiveScenarioGraphs.level_ai_configure_environment_node_count =
		level_ai_configure_environment_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_target2_less_than_node_count =
		level_ai_if_distance_to_target2_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_target2_greater_than_node_count =
		level_ai_if_distance_to_target2_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_speak_node_count =
		level_ai_speak_node_count;
	s_ActiveScenarioGraphs.level_ai_play_sound_node_count =
		level_ai_play_sound_node_count;
	s_ActiveScenarioGraphs.level_ai_assign_sound_node_count =
		level_ai_assign_sound_node_count;
	s_ActiveScenarioGraphs.level_ai_audio_mute_channel_node_count =
		level_ai_audio_mute_channel_node_count;
	s_ActiveScenarioGraphs.level_ai_if_channel_free_node_count =
		level_ai_if_channel_free_node_count;
	s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_node_count =
		level_ai_set_object_sound_volume_node_count;
	s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_by_distance_node_count =
		level_ai_set_object_sound_volume_by_distance_node_count;
	s_ActiveScenarioGraphs.level_ai_set_object_sound_playing_node_count =
		level_ai_set_object_sound_playing_node_count;
	s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_object_node_count =
		level_ai_play_repeating_sound_from_object_node_count;
	s_ActiveScenarioGraphs.level_ai_play_sound_from_entity_node_count =
		level_ai_play_sound_from_entity_node_count;
	s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_pad_node_count =
		level_ai_play_repeating_sound_from_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_if_object_sound_volume_less_than_node_count =
		level_ai_if_object_sound_volume_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_play_sound_from_prop_node_count =
		level_ai_play_sound_from_prop_node_count;
	s_ActiveScenarioGraphs.level_ai_play_temporary_primary_track_node_count =
		level_ai_play_temporary_primary_track_node_count;
	s_ActiveScenarioGraphs.level_ai_play_x_track_node_count =
		level_ai_play_x_track_node_count;
	s_ActiveScenarioGraphs.level_ai_stop_x_track_node_count =
		level_ai_stop_x_track_node_count;
	s_ActiveScenarioGraphs.level_ai_play_track_isolated_node_count =
		level_ai_play_track_isolated_node_count;
	s_ActiveScenarioGraphs.level_ai_play_default_tracks_node_count =
		level_ai_play_default_tracks_node_count;
	s_ActiveScenarioGraphs.level_ai_play_cutscene_track_node_count =
		level_ai_play_cutscene_track_node_count;
	s_ActiveScenarioGraphs.level_ai_stop_cutscene_track_node_count =
		level_ai_stop_cutscene_track_node_count;
	s_ActiveScenarioGraphs.level_ai_play_temporary_track_node_count =
		level_ai_play_temporary_track_node_count;
	s_ActiveScenarioGraphs.level_ai_stop_ambient_track_node_count =
		level_ai_stop_ambient_track_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_node_count =
		level_ai_chr_draw_weapon_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_in_cutscene_node_count =
		level_ai_chr_draw_weapon_in_cutscene_node_count;
	s_ActiveScenarioGraphs.level_ai_set_player_force_speed_node_count =
		level_ai_set_player_force_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_invincible_node_count =
		level_ai_chr_set_invincible_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_is_invincible_node_count =
		level_ai_if_player_is_invincible_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_has_no_gun_node_count =
		level_ai_if_chr_has_no_gun_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_delete_weapon_node_count =
		level_ai_chr_delete_weapon_node_count;
	s_ActiveScenarioGraphs.level_ai_if_trigger_shot_list_node_count =
		level_ai_if_trigger_shot_list_node_count;
	s_ActiveScenarioGraphs.level_ai_end_level_node_count =
		level_ai_end_level_node_count;
	s_ActiveScenarioGraphs.level_ai_end_cutscene_node_count =
		level_ai_end_cutscene_node_count;
	s_ActiveScenarioGraphs.level_ai_warp_jo_to_pad_node_count =
		level_ai_warp_jo_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_set_camera_animation_node_count =
		level_ai_set_camera_animation_node_count;
	s_ActiveScenarioGraphs.level_ai_if_in_cutscene_node_count =
		level_ai_if_in_cutscene_node_count;
	s_ActiveScenarioGraphs.level_ai_if_cutscene_button_pressed_node_count =
		level_ai_if_cutscene_button_pressed_node_count;
	s_ActiveScenarioGraphs.level_ai_reorient_for_cutscene_stop_node_count =
		level_ai_reorient_for_cutscene_stop_node_count;
	s_ActiveScenarioGraphs.level_ai_warp_jo_to_tag_node_count =
		level_ai_warp_jo_to_tag_node_count;
	s_ActiveScenarioGraphs.level_ai_revoke_control_node_count =
		level_ai_revoke_control_node_count;
	s_ActiveScenarioGraphs.level_ai_grant_control_node_count =
		level_ai_grant_control_node_count;
	s_ActiveScenarioGraphs.level_ai_player_fade_in_node_count =
		level_ai_player_fade_in_node_count;
	s_ActiveScenarioGraphs.level_ai_players_fade_out_node_count =
		level_ai_players_fade_out_node_count;
	s_ActiveScenarioGraphs.level_ai_if_colour_fade_complete_node_count =
		level_ai_if_colour_fade_complete_node_count;
	s_ActiveScenarioGraphs.level_ai_prepare_warp_orbit_node_count =
		level_ai_prepare_warp_orbit_node_count;
	s_ActiveScenarioGraphs.level_ai_begin_warp_latch_node_count =
		level_ai_begin_warp_latch_node_count;
	s_ActiveScenarioGraphs.level_ai_if_warp_latch_complete_node_count =
		level_ai_if_warp_latch_complete_node_count;
	s_ActiveScenarioGraphs.level_ai_spawn_chr_at_pad_node_count =
		level_ai_spawn_chr_at_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_spawn_chr_at_chr_node_count =
		level_ai_spawn_chr_at_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_try_equip_weapon_node_count =
		level_ai_try_equip_weapon_node_count;
	s_ActiveScenarioGraphs.level_ai_try_equip_hat_node_count =
		level_ai_try_equip_hat_node_count;
	s_ActiveScenarioGraphs.level_ai_set_obj_image_node_count =
		level_ai_set_obj_image_node_count;
	s_ActiveScenarioGraphs.level_ai_object_do_animation_node_count =
		level_ai_object_do_animation_node_count;
	s_ActiveScenarioGraphs.level_ai_set_door_open_node_count =
		level_ai_set_door_open_node_count;
	s_ActiveScenarioGraphs.level_ai_duplicate_chr_node_count =
		level_ai_duplicate_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_enable_chr_node_count =
		level_ai_enable_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_disable_chr_node_count =
		level_ai_disable_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_enable_obj_node_count =
		level_ai_enable_obj_node_count;
	s_ActiveScenarioGraphs.level_ai_disable_obj_node_count =
		level_ai_disable_obj_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_move_to_pad_node_count =
		level_ai_chr_move_to_pad_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_team_node_count =
		level_ai_chr_set_team_node_count;
	s_ActiveScenarioGraphs.level_ai_damage_chr_by_amount_node_count =
		level_ai_damage_chr_by_amount_node_count;
	s_ActiveScenarioGraphs.level_ai_do_preset_animation_node_count =
		level_ai_do_preset_animation_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_chr_portal_distance_less_than_node_count =
		level_ai_if_player_chr_portal_distance_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_reposition_valid_node_count =
		level_ai_if_chr_reposition_valid_node_count;
	s_ActiveScenarioGraphs.level_ai_do_gun_command_node_count =
		level_ai_do_gun_command_node_count;
	s_ActiveScenarioGraphs.level_ai_if_distance_to_gun_less_than_node_count =
		level_ai_if_distance_to_gun_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_recover_gun_node_count =
		level_ai_recover_gun_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_copy_properties_node_count =
		level_ai_chr_copy_properties_node_count;
	s_ActiveScenarioGraphs.level_ai_player_auto_walk_node_count =
		level_ai_player_auto_walk_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_auto_walk_finished_node_count =
		level_ai_if_player_auto_walk_finished_node_count;
	s_ActiveScenarioGraphs.level_ai_if_obj_in_room_node_count =
		level_ai_if_obj_in_room_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_looking_at_object_node_count =
		level_ai_if_player_looking_at_object_node_count;
	s_ActiveScenarioGraphs.level_ai_if_target_is_player_node_count =
		level_ai_if_target_is_player_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_kill_node_count =
		level_ai_chr_kill_node_count;
	s_ActiveScenarioGraphs.level_ai_remove_weapon_from_inventory_node_count =
		level_ai_remove_weapon_from_inventory_node_count;
	s_ActiveScenarioGraphs.level_ai_clear_inventory_node_count =
		level_ai_clear_inventory_node_count;
	s_ActiveScenarioGraphs.level_ai_release_object_node_count =
		level_ai_release_object_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_grab_object_node_count =
		level_ai_chr_grab_object_node_count;
	s_ActiveScenarioGraphs.level_ai_toggle_p1p2_node_count =
		level_ai_toggle_p1p2_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_p1p2_node_count =
		level_ai_chr_set_p1p2_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_cloaked_node_count =
		level_ai_chr_set_cloaked_node_count;
	s_ActiveScenarioGraphs.level_ai_set_autogun_target_team_node_count =
		level_ai_set_autogun_target_team_node_count;
	s_ActiveScenarioGraphs.level_ai_if_objective_complete_node_count =
		level_ai_if_objective_complete_node_count;
	s_ActiveScenarioGraphs.level_ai_if_objective_failed_node_count =
		level_ai_if_objective_failed_node_count;
	s_ActiveScenarioGraphs.level_ai_if_all_objectives_complete_node_count =
		level_ai_if_all_objectives_complete_node_count;
	s_ActiveScenarioGraphs.level_ai_if_difficulty_less_than_node_count =
		level_ai_if_difficulty_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_difficulty_greater_than_node_count =
		level_ai_if_difficulty_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stage_timer_less_than_node_count =
		level_ai_if_stage_timer_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stage_timer_greater_than_node_count =
		level_ai_if_stage_timer_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stage_id_less_than_node_count =
		level_ai_if_stage_id_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_stage_id_greater_than_node_count =
		level_ai_if_stage_id_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_waypoint_within_quadrant_node_count =
		level_ai_if_waypoint_within_quadrant_node_count;
	s_ActiveScenarioGraphs.level_ai_set_pad_preset_to_target_quadrant_node_count =
		level_ai_set_pad_preset_to_target_quadrant_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_players_less_than_node_count =
		level_ai_if_num_players_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_kill_count_greater_than_node_count =
		level_ai_if_kill_count_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_num_knocked_out_chrs_node_count =
		level_ai_if_num_knocked_out_chrs_node_count;
	s_ActiveScenarioGraphs.level_ai_kill_bond_node_count =
		level_ai_kill_bond_node_count;
	s_ActiveScenarioGraphs.level_ai_if_pouncebits_eq_node_count =
		level_ai_if_pouncebits_eq_node_count;
	s_ActiveScenarioGraphs.level_ai_if_training_pc_holographed_node_count =
		level_ai_if_training_pc_holographed_node_count;
	s_ActiveScenarioGraphs.level_ai_if_player_using_device_node_count =
		level_ai_if_player_using_device_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_begin_or_end_teleport_node_count =
		level_ai_chr_begin_or_end_teleport_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_teleport_full_white_node_count =
		level_ai_if_chr_teleport_full_white_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_cutscene_weapon_node_count =
		level_ai_chr_set_cutscene_weapon_node_count;
	s_ActiveScenarioGraphs.level_ai_fade_screen_node_count =
		level_ai_fade_screen_node_count;
	s_ActiveScenarioGraphs.level_ai_if_fade_complete_node_count =
		level_ai_if_fade_complete_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_hudpiece_visible_node_count =
		level_ai_set_chr_hudpiece_visible_node_count;
	s_ActiveScenarioGraphs.level_ai_set_passive_mode_node_count =
		level_ai_set_passive_mode_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_set_firing_in_cutscene_node_count =
		level_ai_chr_set_firing_in_cutscene_node_count;
	s_ActiveScenarioGraphs.level_ai_set_portal_flag_node_count =
		level_ai_set_portal_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_music_event_queue_is_empty_node_count =
		level_ai_if_music_event_queue_is_empty_node_count;
	s_ActiveScenarioGraphs.level_ai_if_coop_mode_node_count =
		level_ai_if_coop_mode_node_count;
	s_ActiveScenarioGraphs.level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count =
		level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_remove_references_to_chr_node_count =
		level_ai_remove_references_to_chr_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_toggle_model_part_node_count =
		level_ai_chr_toggle_model_part_node_count;
	s_ActiveScenarioGraphs.level_ai_obj_set_model_part_visible_node_count =
		level_ai_obj_set_model_part_visible_node_count;
	s_ActiveScenarioGraphs.level_ai_if_obj_health_less_than_node_count =
		level_ai_if_obj_health_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_set_obj_health_node_count =
		level_ai_set_obj_health_node_count;
	s_ActiveScenarioGraphs.level_ai_set_chr_special_death_animation_node_count =
		level_ai_set_chr_special_death_animation_node_count;
	s_ActiveScenarioGraphs.level_ai_set_room_to_search_node_count =
		level_ai_set_room_to_search_node_count;
	s_ActiveScenarioGraphs.level_ai_restart_timer_node_count =
		level_ai_restart_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_reset_timer_node_count =
		level_ai_reset_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_pause_timer_node_count =
		level_ai_pause_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_resume_timer_node_count =
		level_ai_resume_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_if_timer_stopped_node_count =
		level_ai_if_timer_stopped_node_count;
	s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_random_node_count =
		level_ai_if_timer_greater_than_random_node_count;
	s_ActiveScenarioGraphs.level_ai_if_timer_less_than_node_count =
		level_ai_if_timer_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_node_count =
		level_ai_if_timer_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_show_countdown_timer_node_count =
		level_ai_show_countdown_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_hide_countdown_timer_node_count =
		level_ai_hide_countdown_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_set_countdown_timer_node_count =
		level_ai_set_countdown_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_stop_countdown_timer_node_count =
		level_ai_stop_countdown_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_start_countdown_timer_node_count =
		level_ai_start_countdown_timer_node_count;
	s_ActiveScenarioGraphs.level_ai_if_countdown_timer_stopped_node_count =
		level_ai_if_countdown_timer_stopped_node_count;
	s_ActiveScenarioGraphs.level_ai_if_countdown_timer_less_than_node_count =
		level_ai_if_countdown_timer_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_if_countdown_timer_greater_than_node_count =
		level_ai_if_countdown_timer_greater_than_node_count;
	s_ActiveScenarioGraphs.level_ai_set_savefile_flag_node_count =
		level_ai_set_savefile_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_unset_savefile_flag_node_count =
		level_ai_unset_savefile_flag_node_count;
	s_ActiveScenarioGraphs.level_ai_if_savefile_flag_set_node_count =
		level_ai_if_savefile_flag_set_node_count;
	s_ActiveScenarioGraphs.level_ai_if_savefile_flag_unset_node_count =
		level_ai_if_savefile_flag_unset_node_count;
	s_ActiveScenarioGraphs.level_ai_show_hudmsg_node_count =
		level_ai_show_hudmsg_node_count;
	s_ActiveScenarioGraphs.level_ai_show_hudmsg_middle_node_count =
		level_ai_show_hudmsg_middle_node_count;
	s_ActiveScenarioGraphs.level_ai_show_hudmsg_top_middle_node_count =
		level_ai_show_hudmsg_top_middle_node_count;
	s_ActiveScenarioGraphs.level_ai_hovercar_begin_path_node_count =
		level_ai_hovercar_begin_path_node_count;
	s_ActiveScenarioGraphs.level_ai_set_vehicle_speed_node_count =
		level_ai_set_vehicle_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_set_rotor_speed_node_count =
		level_ai_set_rotor_speed_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_explosions_node_count =
		level_ai_chr_explosions_node_count;
	s_ActiveScenarioGraphs.level_ai_set_tinted_glass_enabled_node_count =
		level_ai_set_tinted_glass_enabled_node_count;
	s_ActiveScenarioGraphs.level_ai_hovercopter_fire_rocket_node_count =
		level_ai_hovercopter_fire_rocket_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_adjust_motion_blur_node_count =
		level_ai_chr_adjust_motion_blur_node_count;
	s_ActiveScenarioGraphs.level_ai_punch_or_kick_node_count =
		level_ai_punch_or_kick_node_count;
	s_ActiveScenarioGraphs
		.level_ai_set_target_to_eyespy_if_in_sight_node_count =
		level_ai_set_target_to_eyespy_if_in_sight_node_count;
	s_ActiveScenarioGraphs.level_ai_mini_skedar_try_pounce_node_count =
		level_ai_mini_skedar_try_pounce_node_count;
	s_ActiveScenarioGraphs
		.level_ai_if_object_distance_to_pad_less_than_node_count =
		level_ai_if_object_distance_to_pad_less_than_node_count;
	s_ActiveScenarioGraphs.level_ai_avoid_node_count =
		level_ai_avoid_node_count;
	s_ActiveScenarioGraphs.level_ai_title_init_mode_node_count =
		level_ai_title_init_mode_node_count;
	s_ActiveScenarioGraphs.level_ai_try_exit_title_node_count =
		level_ai_try_exit_title_node_count;
	s_ActiveScenarioGraphs.level_ai_chr_emit_sparks_node_count =
		level_ai_chr_emit_sparks_node_count;
	s_ActiveScenarioGraphs.level_ai_set_dr_caroll_images_node_count =
		level_ai_set_dr_caroll_images_node_count;
	s_ActiveScenarioGraphs.level_ai_say_quip_node_count =
		level_ai_say_quip_node_count;
	s_ActiveScenarioGraphs.level_ai_say_ci_staff_quip_node_count =
		level_ai_say_ci_staff_quip_node_count;
	s_ActiveScenarioGraphs.level_ai_shuffle_ruins_pillars_node_count =
		level_ai_shuffle_ruins_pillars_node_count;
	s_ActiveScenarioGraphs.level_ai_shuffle_pelagic_switches_node_count =
		level_ai_shuffle_pelagic_switches_node_count;
	s_ActiveScenarioGraphs.level_graph_active = 1;

	stageid = stage && stage->entry && stage->entry->id[0]
		? stage->entry->id : "?";
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: activated level graph '%s' path=%s bytes=%u stage='%s'",
		scenario->id, graph_path, (unsigned)text_size, stageid);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: table refs '%s' pads=%s spawns=%s setup=%s ai=%s objects=%s volumes=%s objectives=%s waypoints=%s waygroups=%s covers=%s paths=%s",
		scenario->id,
		s_ActiveScenarioGraphs.pads_path,
		s_ActiveScenarioGraphs.spawns_path,
		s_ActiveScenarioGraphs.setup_fields_path,
		s_ActiveScenarioGraphs.ai_lists_path,
		s_ActiveScenarioGraphs.objects_path,
		s_ActiveScenarioGraphs.volumes_path,
		s_ActiveScenarioGraphs.objectives_path,
		s_ActiveScenarioGraphs.waypoints_path,
		s_ActiveScenarioGraphs.waygroups_path,
		s_ActiveScenarioGraphs.covers_path,
		s_ActiveScenarioGraphs.paths_path);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: global settings source '%s' nodes=%d scenario='%s' kind='%s' backend=graph.global.settings+level.graph.nodes",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_global_settings_node_count,
		s_ActiveScenarioGraphs.level_global_settings_scenario,
		s_ActiveScenarioGraphs.level_global_settings_kind[0]
			? s_ActiveScenarioGraphs.level_global_settings_kind : "(unknown)");
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: volume source '%s' volumes=%d backend=graph.trigger.volumes+volumes.tsv",
		s_ActiveScenarioGraphs.volumes_path,
		s_ActiveScenarioGraphs.level_volume_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: pad source '%s' nodes=%d backend=graph.pads+pads.tsv",
		s_ActiveScenarioGraphs.pads_path,
		s_ActiveScenarioGraphs.level_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: trigger volume nodes '%s' nodes=%d rows=%d backend=graph.trigger.volumes+level.graph.nodes+volumes.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_volume_node_count,
		s_ActiveScenarioGraphs.level_volume_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI list source '%s' nodes=%d backend=graph.ai.lists+ai/ailists.tsv",
		s_ActiveScenarioGraphs.ai_lists_path,
		s_ActiveScenarioGraphs.level_ai_list_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI basic/lifecycle actions '%s' stop=%d kneel=%d surrender=%d fade_out=%d remove_chr=%d backend=graph.ai.action.character_lifecycle+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_stop_node_count,
		s_ActiveScenarioGraphs.level_ai_kneel_node_count,
		s_ActiveScenarioGraphs.level_ai_surrender_node_count,
		s_ActiveScenarioGraphs.level_ai_fade_out_node_count,
		s_ActiveScenarioGraphs.level_ai_remove_chr_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI combat actions '%s' sidestep=%d jump_out=%d run_sideways=%d attack_walk=%d attack_run=%d attack_roll=%d attack_stand=%d attack_kneel=%d attack_lie=%d if_attack_locked=%d if_attacking=%d modify=%d face_entity=%d apply_gset_damage=%d chr_damage_chr=%d consider_grenade_throw=%d drop_item=%d backend=graph.ai.action.combat+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_try_sidestep_node_count,
		s_ActiveScenarioGraphs.level_ai_try_jump_out_node_count,
		s_ActiveScenarioGraphs.level_ai_try_run_sideways_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_walk_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_run_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_roll_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_stand_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_kneel_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_lie_node_count,
		s_ActiveScenarioGraphs.level_ai_if_attack_locked_node_count,
		s_ActiveScenarioGraphs.level_ai_if_attacking_node_count,
		s_ActiveScenarioGraphs.level_ai_try_modify_attack_node_count,
		s_ActiveScenarioGraphs.level_ai_face_entity_node_count,
		s_ActiveScenarioGraphs.level_ai_apply_gset_damage_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_damage_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_consider_grenade_throw_node_count,
		s_ActiveScenarioGraphs.level_ai_drop_item_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI target movement actions '%s' run_from_target=%d jog_to_target_prop=%d walk_to_target_prop=%d run_to_target_prop=%d go_to_cover_prop=%d jog_to_chr=%d walk_to_chr=%d run_to_chr=%d backend=graph.ai.action.target_movement+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_try_run_from_target_node_count,
		s_ActiveScenarioGraphs.level_ai_try_jog_to_target_prop_node_count,
		s_ActiveScenarioGraphs.level_ai_try_walk_to_target_prop_node_count,
		s_ActiveScenarioGraphs.level_ai_try_run_to_target_prop_node_count,
		s_ActiveScenarioGraphs.level_ai_try_go_to_cover_prop_node_count,
		s_ActiveScenarioGraphs.level_ai_try_jog_to_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_try_walk_to_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_try_run_to_chr_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI perception/alarm actions '%s' hear_alarm=%d patrolling=%d alarm_active=%d gas_active=%d hears_target=%d saw_injury=%d saw_death=%d los_target=%d los_attack_target=%d target_nearly_in_sight=%d nearly_in_targets_sight=%d set_pad_route=%d saw_target_recently=%d heard_target_recently=%d backend=graph.ai.condition.perception_alarm+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_can_hear_alarm_node_count,
		s_ActiveScenarioGraphs.level_ai_if_patrolling_node_count,
		s_ActiveScenarioGraphs.level_ai_if_alarm_active_node_count,
		s_ActiveScenarioGraphs.level_ai_if_gas_active_node_count,
		s_ActiveScenarioGraphs.level_ai_if_hears_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_saw_injury_node_count,
		s_ActiveScenarioGraphs.level_ai_if_saw_death_node_count,
		s_ActiveScenarioGraphs.level_ai_if_los_to_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_los_to_attack_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_nearly_in_sight_node_count,
		s_ActiveScenarioGraphs.level_ai_if_nearly_in_targets_sight_node_count,
		s_ActiveScenarioGraphs.level_ai_set_pad_preset_to_pad_on_route_to_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_saw_target_recently_node_count,
		s_ActiveScenarioGraphs.level_ai_if_heard_target_recently_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI spatial perception conditions '%s' los_chr=%d never_screen=%d on_screen=%d chr_room_screen=%d room_screen=%d target_aiming=%d near_miss=%d suspicious_item=%d check_fov=%d fov_left=%d out_fov_left=%d target_fov=%d target_out_fov=%d dist_lt=%d dist_gt=%d backend=graph.ai.condition.spatial_perception+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_los_to_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_if_never_been_on_screen_node_count,
		s_ActiveScenarioGraphs.level_ai_if_on_screen_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_in_on_screen_room_node_count,
		s_ActiveScenarioGraphs.level_ai_if_room_is_on_screen_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_aiming_at_me_node_count,
		s_ActiveScenarioGraphs.level_ai_if_near_miss_node_count,
		s_ActiveScenarioGraphs.level_ai_if_sees_suspicious_item_node_count,
		s_ActiveScenarioGraphs.level_ai_if_check_fov_with_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_in_fov_left_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_left_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_in_fov_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_node_count,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_target_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_target_greater_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI distance perception conditions '%s' chr_pad_lt=%d chr_pad_gt=%d dist_chr_lt=%d dist_chr_gt=%d any_chr_near=%d target_pad_lt=%d target_pad_gt=%d backend=graph.ai.condition.distance_perception+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs
			.level_ai_if_chr_distance_to_pad_less_than_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_chr_distance_to_pad_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_chr_less_than_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_distance_to_chr_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_any_chr_near_self_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_distance_from_target_to_pad_less_than_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_distance_from_target_to_pad_greater_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI room/object/weapon conditions '%s' chr_room=%d target_room=%d chr_object=%d weapon_thrown=%d weapon_on_object=%d chr_weapon=%d gun_unclaimed=%d object_healthy=%d backend=graph.ai.condition.room_object_weapon+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_chr_in_room_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_in_room_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_object_node_count,
		s_ActiveScenarioGraphs.level_ai_if_weapon_thrown_node_count,
		s_ActiveScenarioGraphs.level_ai_if_weapon_thrown_on_object_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_weapon_equipped_node_count,
		s_ActiveScenarioGraphs.level_ai_if_gun_unclaimed_node_count,
		s_ActiveScenarioGraphs.level_ai_if_object_healthy_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI object interaction actions '%s' activated=%d interact=%d destroy=%d drop_object=%d drop_items=%d drop_weapon=%d give_object=%d move_to_pad=%d backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_chr_activated_object_node_count,
		s_ActiveScenarioGraphs.level_ai_obj_interact_node_count,
		s_ActiveScenarioGraphs.level_ai_destroy_object_node_count,
		s_ActiveScenarioGraphs.level_ai_drop_object_from_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_drop_items_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_drop_weapon_node_count,
		s_ActiveScenarioGraphs.level_ai_give_object_to_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_object_move_to_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI animation actions '%s' chr_do_animation=%d surprise_one_hand=%d surprise_look_around=%d surprise_surrender=%d backend=graph.ai.action.animation+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_do_animation_node_count,
		s_ActiveScenarioGraphs.level_ai_be_surprised_one_hand_node_count,
		s_ActiveScenarioGraphs.level_ai_be_surprised_look_around_node_count,
		s_ActiveScenarioGraphs.level_ai_be_surprised_surrender_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI random control '%s' random=%d less_than=%d greater_than=%d backend=graph.ai.control.random+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_random_node_count,
		s_ActiveScenarioGraphs.level_ai_if_random_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_random_greater_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI debug/no-op actions '%s' print=%d noop=%d backend=graph.ai.action.debug_noop+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_print_node_count,
		s_ActiveScenarioGraphs.level_ai_noop_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI list-control actions '%s' set_list=%d set_return_list=%d set_shot_list=%d return_list=%d set_punch_dodge_list=%d set_shooting_at_me_list=%d set_dark_room_list=%d set_player_dead_list=%d backend=graph.ai.action.list_control+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_return_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_shot_list_node_count,
		s_ActiveScenarioGraphs.level_ai_return_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_punch_dodge_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_shooting_at_me_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_dark_room_list_node_count,
		s_ActiveScenarioGraphs.level_ai_set_player_dead_list_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: path source '%s' nodes=%d backend=graph.navigation.paths+navigation/paths.tsv",
		s_ActiveScenarioGraphs.paths_path,
		s_ActiveScenarioGraphs.level_path_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad actions '%s' walk_to_pad=%d run_to_pad=%d backend=graph.ai.action.pad+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad movement actions '%s' jog_to_pad=%d go_to_pad_preset=%d backend=graph.ai.action.pad+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI path actions '%s' set_path=%d start_patrol=%d backend=graph.ai.action.path+navigation/paths.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_path_node_count,
		s_ActiveScenarioGraphs.level_ai_start_patrol_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad-preset actions '%s' set_pad_preset=%d chr_set_pad_preset=%d chr_copy_pad_preset=%d backend=graph.ai.action.pad_preset+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_pad_preset_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_pad_preset_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_copy_pad_preset_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI chr-preset actions '%s' set_chr_preset=%d set_chr_target=%d backend=graph.ai.action.chr_preset+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_chr_preset_node_count,
		s_ActiveScenarioGraphs.level_ai_set_chr_target_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI morale/alertness actions '%s' set_morale=%d add_morale=%d chr_add_morale=%d subtract_morale=%d set_alertness=%d add_alertness=%d chr_add_alertness=%d subtract_alertness=%d increase_squadron_alertness=%d backend=graph.ai.action.state+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_morale_node_count,
		s_ActiveScenarioGraphs.level_ai_add_morale_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_add_morale_node_count,
		s_ActiveScenarioGraphs.level_ai_subtract_morale_node_count,
		s_ActiveScenarioGraphs.level_ai_set_alertness_node_count,
		s_ActiveScenarioGraphs.level_ai_add_alertness_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_add_alertness_node_count,
		s_ActiveScenarioGraphs.level_ai_subtract_alertness_node_count,
		s_ActiveScenarioGraphs.level_ai_increase_squadron_alertness_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI character conditions '%s' if_num_arghs_less_than=%d if_num_arghs_greater_than=%d if_num_close_arghs_less_than=%d if_num_close_arghs_greater_than=%d if_chr_health_greater_than=%d if_chr_health_less_than=%d if_chr_shield_less_than=%d if_chr_shield_greater_than=%d if_injured=%d if_shield_damaged=%d if_morale_less_than=%d if_morale_less_than_random=%d if_alertness=%d if_chr_alertness_less_than=%d if_alertness_less_than_random=%d backend=graph.ai.condition.character_state+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_num_arghs_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_num_arghs_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_num_close_arghs_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_num_close_arghs_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_health_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_health_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_shield_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_shield_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_injured_node_count,
		s_ActiveScenarioGraphs.level_ai_if_shield_damaged_node_count,
		s_ActiveScenarioGraphs.level_ai_if_morale_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_morale_less_than_random_node_count,
		s_ActiveScenarioGraphs.level_ai_if_alertness_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_alertness_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_alertness_less_than_random_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI lifecycle/perception conditions '%s' if_idle=%d if_stopped=%d if_chr_dead=%d if_chr_death_animation_finished=%d if_chr_knocked_out=%d if_can_see_target=%d backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_idle_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stopped_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_dead_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_death_animation_finished_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_knocked_out_node_count,
		s_ActiveScenarioGraphs.level_ai_if_can_see_target_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI tuning actions '%s' set_hear_distance=%d set_view_distance=%d set_grenade_probability=%d set_chr_num=%d set_max_damage=%d add_health=%d set_shield=%d set_reaction_speed=%d set_recovery_speed=%d set_accuracy=%d set_dodge_rating=%d set_unarmed_dodge_rating=%d backend=graph.ai.action.tuning+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_hear_distance_node_count,
		s_ActiveScenarioGraphs.level_ai_set_view_distance_node_count,
		s_ActiveScenarioGraphs.level_ai_set_grenade_probability_node_count,
		s_ActiveScenarioGraphs.level_ai_set_chr_num_node_count,
		s_ActiveScenarioGraphs.level_ai_set_max_damage_node_count,
		s_ActiveScenarioGraphs.level_ai_add_health_node_count,
		s_ActiveScenarioGraphs.level_ai_set_shield_node_count,
		s_ActiveScenarioGraphs.level_ai_set_reaction_speed_node_count,
		s_ActiveScenarioGraphs.level_ai_set_recovery_speed_node_count,
		s_ActiveScenarioGraphs.level_ai_set_accuracy_node_count,
		s_ActiveScenarioGraphs.level_ai_set_dodge_rating_node_count,
		s_ActiveScenarioGraphs.level_ai_set_unarmed_dodge_rating_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI action/order actions '%s' set_action=%d set_team_orders=%d retreat=%d set_squadron=%d chr_set_listening=%d try_attack_amount=%d backend=graph.ai.action.orders+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_action_node_count,
		s_ActiveScenarioGraphs.level_ai_set_team_orders_node_count,
		s_ActiveScenarioGraphs.level_ai_retreat_node_count,
		s_ActiveScenarioGraphs.level_ai_set_squadron_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_listening_node_count,
		s_ActiveScenarioGraphs.level_ai_try_attack_amount_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI intent/status conditions '%s' not_talking=%d orders=%d has_orders=%d squadron_action=%d chr_listening=%d not_listening=%d injured_target=%d action=%d ammo_less=%d chr_target=%d preset_team=%d human=%d skedar=%d prop_sight=%d remove_prop=%d prop_height=%d set_target=%d preset_target=%d preset_near_self=%d preset_near_pad=%d backend=graph.ai.condition.intent_status+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_chr_not_talking_node_count,
		s_ActiveScenarioGraphs.level_ai_if_orders_node_count,
		s_ActiveScenarioGraphs.level_ai_if_has_orders_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_in_squadron_doing_action_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_listening_node_count,
		s_ActiveScenarioGraphs.level_ai_if_not_listening_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_injured_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_action_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_ammo_quantity_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_target_node_count,
		s_ActiveScenarioGraphs.level_ai_if_compare_chr_presets_team_node_count,
		s_ActiveScenarioGraphs.level_ai_if_human_node_count,
		s_ActiveScenarioGraphs.level_ai_if_skedar_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_prop_preset_blocking_sight_to_target_node_count,
		s_ActiveScenarioGraphs.level_ai_remove_object_at_prop_preset_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_prop_preset_height_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_set_target_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_presets_target_is_not_my_target_node_count,
		s_ActiveScenarioGraphs
			.level_ai_set_chr_preset_to_chr_near_self_node_count,
		s_ActiveScenarioGraphs
			.level_ai_set_chr_preset_to_chr_near_pad_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI cover actions '%s' find_cover=%d find_cover_within_dist=%d find_cover_outside_dist=%d go_to_cover=%d check_cover_out_of_sight=%d orbit_target=%d face_cover=%d danger_cover=%d release_cover=%d backend=graph.ai.action.cover+navigation/covers.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_find_cover_node_count,
		s_ActiveScenarioGraphs.level_ai_find_cover_within_dist_node_count,
		s_ActiveScenarioGraphs.level_ai_find_cover_outside_dist_node_count,
		s_ActiveScenarioGraphs.level_ai_go_to_cover_node_count,
		s_ActiveScenarioGraphs.level_ai_check_cover_out_of_sight_node_count,
		s_ActiveScenarioGraphs.level_ai_orbit_target_node_count,
		s_ActiveScenarioGraphs.level_ai_face_cover_node_count,
		s_ActiveScenarioGraphs.level_ai_danger_cover_node_count,
		s_ActiveScenarioGraphs.level_ai_release_cover_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI vehicle/investigation actions '%s' danger_object=%d heli_armed=%d hoverbot_next_step=%d shuffle_investigation=%d set_investigation_pad=%d heli_arm=%d heli_unarm=%d backend=graph.ai.action.vehicle_investigation+ai/ailists.tsv+objects.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_dangerous_object_nearby_node_count,
		s_ActiveScenarioGraphs.level_ai_if_heli_weapons_armed_node_count,
		s_ActiveScenarioGraphs.level_ai_if_hoverbot_next_step_node_count,
		s_ActiveScenarioGraphs
			.level_ai_shuffle_investigation_terminals_node_count,
		s_ActiveScenarioGraphs
			.level_ai_set_pad_preset_to_investigation_terminal_node_count,
		s_ActiveScenarioGraphs.level_ai_heli_arm_weapons_node_count,
		s_ActiveScenarioGraphs.level_ai_heli_unarm_weapons_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI safety/detection conditions '%s' safety2=%d player_cmp_ar34=%d detect_same_floor=%d detect_enemy=%d safety=%d target_slow=%d target_closer=%d target_away=%d backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_safety2_less_than_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_player_using_cmp_or_ar34_node_count,
		s_ActiveScenarioGraphs
			.level_ai_detect_enemy_on_same_floor_node_count,
		s_ActiveScenarioGraphs.level_ai_detect_enemy_node_count,
		s_ActiveScenarioGraphs.level_ai_if_safety_less_than_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_target_moving_slowly_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_target_moving_closer_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_target_moving_away_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI misc branch conditions '%s' squadron_dead=%d if_true=%d squadron_count=%d natural_anim=%d y=%d sound_timer=%d target_y_diff=%d backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_squadron_is_dead_node_count,
		s_ActiveScenarioGraphs.level_ai_if_true_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_num_chrs_in_squadron_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_natural_anim_node_count,
		s_ActiveScenarioGraphs.level_ai_if_y_node_count,
		s_ActiveScenarioGraphs.level_ai_if_sound_timer_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_target_y_difference_less_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI misc effect actions '%s' chr_explosions=%d tinted_glass=%d rocket=%d blur=%d punch=%d eyespy=%d skedar_pounce=%d obj_pad_distance=%d avoid=%d title_init=%d title_exit=%d sparks=%d dr_caroll_images=%d backend=graph.ai.action.misc_effect+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_explosions_node_count,
		s_ActiveScenarioGraphs
			.level_ai_set_tinted_glass_enabled_node_count,
		s_ActiveScenarioGraphs
			.level_ai_hovercopter_fire_rocket_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_adjust_motion_blur_node_count,
		s_ActiveScenarioGraphs.level_ai_punch_or_kick_node_count,
		s_ActiveScenarioGraphs
			.level_ai_set_target_to_eyespy_if_in_sight_node_count,
		s_ActiveScenarioGraphs
			.level_ai_mini_skedar_try_pounce_node_count,
		s_ActiveScenarioGraphs
			.level_ai_if_object_distance_to_pad_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_avoid_node_count,
		s_ActiveScenarioGraphs.level_ai_title_init_mode_node_count,
		s_ActiveScenarioGraphs.level_ai_try_exit_title_node_count,
	s_ActiveScenarioGraphs.level_ai_chr_emit_sparks_node_count,
	s_ActiveScenarioGraphs.level_ai_set_dr_caroll_images_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI quip/setup shuffle actions '%s' say_quip=%d ci_staff_quip=%d ruins_pillars=%d pelagic_switches=%d backend=graph.ai.action.quip_shuffle+ai/ailists.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_say_quip_node_count,
		s_ActiveScenarioGraphs.level_ai_say_ci_staff_quip_node_count,
		s_ActiveScenarioGraphs.level_ai_shuffle_ruins_pillars_node_count,
		s_ActiveScenarioGraphs
			.level_ai_shuffle_pelagic_switches_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI team maintenance actions '%s' set_chr_preset_to_unalerted_teammate=%d rebuild_teams=%d rebuild_squadrons=%d backend=graph.ai.action.team+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_chr_preset_to_unalerted_teammate_node_count,
		s_ActiveScenarioGraphs.level_ai_rebuild_teams_node_count,
		s_ActiveScenarioGraphs.level_ai_rebuild_squadrons_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI alarm actions '%s' try_start_alarm=%d activate_alarm=%d deactivate_alarm=%d backend=graph.ai.action.alarm+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_try_start_alarm_node_count,
		s_ActiveScenarioGraphs.level_ai_activate_alarm_node_count,
		s_ActiveScenarioGraphs.level_ai_deactivate_alarm_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI flag actions '%s' set_flag=%d unset_flag=%d if_has_flag=%d chr_set_flag=%d chr_unset_flag=%d if_chr_has_flag=%d set_stage_flag=%d unset_stage_flag=%d if_stage_flag_eq=%d backend=graph.ai.action.flags+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_unset_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_has_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_unset_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_set_stage_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_unset_stage_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stage_flag_eq_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI chr/object flag actions '%s' set_chrflag=%d unset_chrflag=%d if_has_chrflag=%d chr_set_chrflag=%d chr_unset_chrflag=%d if_chr_has_chrflag=%d chr_set_hidden_flag=%d chr_unset_hidden_flag=%d if_chr_has_hidden_flag=%d set_obj_flag=%d unset_obj_flag=%d if_obj_has_flag=%d backend=graph.ai.action.chr_object_flags+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_unset_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_has_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_unset_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_chrflag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_hidden_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_unset_hidden_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_hidden_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_set_obj_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_unset_obj_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_obj_has_flag_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI door actions '%s' open_door=%d close_door=%d if_door_state=%d if_object_is_door=%d lock_door=%d unlock_door=%d if_door_locked=%d backend=graph.ai.action.door+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_open_door_node_count,
		s_ActiveScenarioGraphs.level_ai_close_door_node_count,
		s_ActiveScenarioGraphs.level_ai_if_door_state_node_count,
		s_ActiveScenarioGraphs.level_ai_if_object_is_door_node_count,
		s_ActiveScenarioGraphs.level_ai_lock_door_node_count,
		s_ActiveScenarioGraphs.level_ai_unlock_door_node_count,
		s_ActiveScenarioGraphs.level_ai_if_door_locked_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI lift actions '%s' if_lift_stationary=%d lift_go_to_stop=%d if_lift_at_stop=%d activate_lift=%d if_using_lift=%d backend=graph.ai.action.lift+objects.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_lift_stationary_node_count,
		s_ActiveScenarioGraphs.level_ai_lift_go_to_stop_node_count,
		s_ActiveScenarioGraphs.level_ai_if_lift_at_stop_node_count,
		s_ActiveScenarioGraphs.level_ai_activate_lift_node_count,
		s_ActiveScenarioGraphs.level_ai_if_using_lift_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI weather actions '%s' configure_rain=%d configure_snow=%d backend=graph.ai.action.weather+ai/ailists.tsv+scenario.ini",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_configure_rain_node_count,
		s_ActiveScenarioGraphs.level_ai_configure_snow_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI sky actions '%s' switch_to_alt_sky=%d set_wind_speed=%d backend=graph.ai.action.sky+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_switch_to_alt_sky_node_count,
		s_ActiveScenarioGraphs.level_ai_set_wind_speed_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI lighting actions '%s' set_lights=%d backend=graph.ai.action.lighting+ai/ailists.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_lights_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI room-flag actions '%s' set_room_flag=%d backend=graph.ai.action.room_flags+ai/ailists.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_room_flag_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI cutscene visibility actions '%s' show_cutscene_chrs=%d backend=graph.ai.action.cutscene_visibility+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_show_cutscene_chrs_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI environment actions '%s' configure_environment=%d backend=graph.ai.action.environment+ai/ailists.tsv+scenario.ini+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_configure_environment_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI target-distance conditions '%s' if_distance_to_target2_less_than=%d if_distance_to_target2_greater_than=%d backend=graph.ai.condition.target_distance+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_target2_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_target2_greater_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI audio actions '%s' speak=%d play_sound=%d assign_sound=%d mute=%d if_channel_free=%d set_volume=%d set_volume_by_distance=%d set_playing=%d repeat_object=%d sound_entity=%d repeat_pad=%d if_volume_less_than=%d play_sound_from_prop=%d play_temporary_primary_track=%d backend=graph.ai.action.audio+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_speak_node_count,
		s_ActiveScenarioGraphs.level_ai_play_sound_node_count,
		s_ActiveScenarioGraphs.level_ai_assign_sound_node_count,
		s_ActiveScenarioGraphs.level_ai_audio_mute_channel_node_count,
		s_ActiveScenarioGraphs.level_ai_if_channel_free_node_count,
		s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_node_count,
		s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_by_distance_node_count,
		s_ActiveScenarioGraphs.level_ai_set_object_sound_playing_node_count,
		s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_object_node_count,
		s_ActiveScenarioGraphs.level_ai_play_sound_from_entity_node_count,
		s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_if_object_sound_volume_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_play_sound_from_prop_node_count,
		s_ActiveScenarioGraphs.level_ai_play_temporary_primary_track_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI music track actions '%s' play_x_track=%d stop_x_track=%d play_track_isolated=%d play_default_tracks=%d play_cutscene_track=%d stop_cutscene_track=%d play_temporary_track=%d stop_ambient_track=%d backend=graph.ai.action.music_track+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_play_x_track_node_count,
		s_ActiveScenarioGraphs.level_ai_stop_x_track_node_count,
		s_ActiveScenarioGraphs.level_ai_play_track_isolated_node_count,
		s_ActiveScenarioGraphs.level_ai_play_default_tracks_node_count,
		s_ActiveScenarioGraphs.level_ai_play_cutscene_track_node_count,
		s_ActiveScenarioGraphs.level_ai_stop_cutscene_track_node_count,
		s_ActiveScenarioGraphs.level_ai_play_temporary_track_node_count,
		s_ActiveScenarioGraphs.level_ai_stop_ambient_track_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI player weapon-state actions '%s' chr_draw_weapon=%d chr_draw_weapon_in_cutscene=%d set_player_force_speed=%d chr_set_invincible=%d if_player_is_invincible=%d if_chr_has_no_gun=%d chr_delete_weapon=%d if_trigger_shot_list=%d backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_in_cutscene_node_count,
		s_ActiveScenarioGraphs.level_ai_set_player_force_speed_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_invincible_node_count,
		s_ActiveScenarioGraphs.level_ai_if_player_is_invincible_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_has_no_gun_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_delete_weapon_node_count,
		s_ActiveScenarioGraphs.level_ai_if_trigger_shot_list_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI player cutscene/warp actions '%s' end_level=%d end_cutscene=%d warp_jo_to_pad=%d set_camera_animation=%d if_in_cutscene=%d if_cutscene_button_pressed=%d reorient_for_cutscene_stop=%d warp_jo_to_tag=%d revoke_control=%d grant_control=%d player_fade_in=%d players_fade_out=%d if_colour_fade_complete=%d prepare_warp_orbit=%d begin_warp_latch=%d if_warp_latch_complete=%d backend=graph.ai.action.player_cutscene+ai/ailists.tsv+pads.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_end_level_node_count,
		s_ActiveScenarioGraphs.level_ai_end_cutscene_node_count,
		s_ActiveScenarioGraphs.level_ai_warp_jo_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_set_camera_animation_node_count,
		s_ActiveScenarioGraphs.level_ai_if_in_cutscene_node_count,
		s_ActiveScenarioGraphs.level_ai_if_cutscene_button_pressed_node_count,
		s_ActiveScenarioGraphs.level_ai_reorient_for_cutscene_stop_node_count,
		s_ActiveScenarioGraphs.level_ai_warp_jo_to_tag_node_count,
		s_ActiveScenarioGraphs.level_ai_revoke_control_node_count,
		s_ActiveScenarioGraphs.level_ai_grant_control_node_count,
		s_ActiveScenarioGraphs.level_ai_player_fade_in_node_count,
		s_ActiveScenarioGraphs.level_ai_players_fade_out_node_count,
		s_ActiveScenarioGraphs.level_ai_if_colour_fade_complete_node_count,
		s_ActiveScenarioGraphs.level_ai_prepare_warp_orbit_node_count,
		s_ActiveScenarioGraphs.level_ai_begin_warp_latch_node_count,
		s_ActiveScenarioGraphs.level_ai_if_warp_latch_complete_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI setup/spawn/equipment actions '%s' spawn_chr_at_pad=%d spawn_chr_at_chr=%d try_equip_weapon=%d try_equip_hat=%d set_obj_image=%d object_do_animation=%d set_door_open=%d backend=graph.ai.action.setup_spawn+ai/ailists.tsv+pads.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_spawn_chr_at_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_spawn_chr_at_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_try_equip_weapon_node_count,
		s_ActiveScenarioGraphs.level_ai_try_equip_hat_node_count,
		s_ActiveScenarioGraphs.level_ai_set_obj_image_node_count,
		s_ActiveScenarioGraphs.level_ai_object_do_animation_node_count,
		s_ActiveScenarioGraphs.level_ai_set_door_open_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI entity lifecycle/motion actions '%s' duplicate_chr=%d enable_chr=%d disable_chr=%d enable_obj=%d disable_obj=%d chr_move_to_pad=%d chr_set_team=%d damage_chr_by_amount=%d do_preset_animation=%d if_player_chr_portal_distance_less_than=%d if_chr_reposition_valid=%d backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv+pads.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_duplicate_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_enable_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_disable_chr_node_count,
		s_ActiveScenarioGraphs.level_ai_enable_obj_node_count,
		s_ActiveScenarioGraphs.level_ai_disable_obj_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_move_to_pad_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_team_node_count,
		s_ActiveScenarioGraphs.level_ai_damage_chr_by_amount_node_count,
		s_ActiveScenarioGraphs.level_ai_do_preset_animation_node_count,
		s_ActiveScenarioGraphs.level_ai_if_player_chr_portal_distance_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_reposition_valid_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI gun interaction actions '%s' do_gun_command=%d if_distance_to_gun_less_than=%d recover_gun=%d backend=graph.ai.action.gun_interaction+ai/ailists.tsv+objects.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_do_gun_command_node_count,
		s_ActiveScenarioGraphs.level_ai_if_distance_to_gun_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_recover_gun_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI character property actions '%s' chr_copy_properties=%d backend=graph.ai.action.character_property+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_copy_properties_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI player navigation actions '%s' player_auto_walk=%d if_player_auto_walk_finished=%d backend=graph.ai.action.player_navigation+ai/ailists.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_player_auto_walk_node_count,
		s_ActiveScenarioGraphs.level_ai_if_player_auto_walk_finished_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI object-room conditions '%s' if_obj_in_room=%d backend=graph.ai.condition.object_room+ai/ailists.tsv+objects.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_obj_in_room_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI perception conditions '%s' if_player_looking_at_object=%d if_target_is_player=%d backend=graph.ai.condition.perception+ai/ailists.tsv+objects.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_player_looking_at_object_node_count,
		s_ActiveScenarioGraphs.level_ai_if_target_is_player_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI character/inventory actions '%s' chr_kill=%d remove_weapon_from_inventory=%d clear_inventory=%d release_object=%d chr_grab_object=%d backend=graph.ai.action.character_inventory+ai/ailists.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_kill_node_count,
		s_ActiveScenarioGraphs.level_ai_remove_weapon_from_inventory_node_count,
		s_ActiveScenarioGraphs.level_ai_clear_inventory_node_count,
		s_ActiveScenarioGraphs.level_ai_release_object_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_grab_object_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI player-state actions '%s' toggle_p1p2=%d chr_set_p1p2=%d chr_set_cloaked=%d set_autogun_target_team=%d backend=graph.ai.action.player_state+ai/ailists.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_toggle_p1p2_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_p1p2_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_cloaked_node_count,
		s_ActiveScenarioGraphs.level_ai_set_autogun_target_team_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI mission/global actions '%s' if_objective_complete=%d if_objective_failed=%d if_all_objectives_complete=%d if_difficulty_less_than=%d if_difficulty_greater_than=%d if_stage_timer_less_than=%d if_stage_timer_greater_than=%d if_stage_id_less_than=%d if_stage_id_greater_than=%d if_num_players_less_than=%d if_kill_count_greater_than=%d if_num_knocked_out_chrs=%d kill_bond=%d backend=graph.ai.action.mission_global+ai/ailists.tsv+mission.graph.json",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_objective_complete_node_count,
		s_ActiveScenarioGraphs.level_ai_if_objective_failed_node_count,
		s_ActiveScenarioGraphs.level_ai_if_all_objectives_complete_node_count,
		s_ActiveScenarioGraphs.level_ai_if_difficulty_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_difficulty_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stage_timer_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stage_timer_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stage_id_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_stage_id_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_num_players_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_kill_count_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_num_knocked_out_chrs_node_count,
		s_ActiveScenarioGraphs.level_ai_kill_bond_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI quadrant pad-preset actions '%s' waypoint_quadrant=%d target_quadrant=%d backend=graph.ai.action.quadrant_preset+ai/ailists.tsv+pads.tsv+navigation.generate",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_waypoint_within_quadrant_node_count,
		s_ActiveScenarioGraphs.level_ai_set_pad_preset_to_target_quadrant_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI state/device conditions '%s' if_pouncebits_eq=%d if_training_pc_holographed=%d if_player_using_device=%d backend=graph.ai.condition.state_device+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_pouncebits_eq_node_count,
		s_ActiveScenarioGraphs.level_ai_if_training_pc_holographed_node_count,
		s_ActiveScenarioGraphs.level_ai_if_player_using_device_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI teleport/cutscene weapon actions '%s' chr_begin_or_end_teleport=%d if_chr_teleport_full_white=%d chr_set_cutscene_weapon=%d backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_begin_or_end_teleport_node_count,
		s_ActiveScenarioGraphs.level_ai_if_chr_teleport_full_white_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_cutscene_weapon_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI cutscene presentation actions '%s' fade_screen=%d if_fade_complete=%d set_chr_hudpiece_visible=%d set_passive_mode=%d chr_set_firing_in_cutscene=%d set_portal_flag=%d backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_fade_screen_node_count,
		s_ActiveScenarioGraphs.level_ai_if_fade_complete_node_count,
		s_ActiveScenarioGraphs.level_ai_set_chr_hudpiece_visible_node_count,
		s_ActiveScenarioGraphs.level_ai_set_passive_mode_node_count,
		s_ActiveScenarioGraphs.level_ai_chr_set_firing_in_cutscene_node_count,
		s_ActiveScenarioGraphs.level_ai_set_portal_flag_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI music/mode conditions '%s' if_music_event_queue_is_empty=%d if_coop_mode=%d backend=graph.ai.condition.music_mode+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_music_event_queue_is_empty_node_count,
		s_ActiveScenarioGraphs.level_ai_if_coop_mode_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI pad/reference actions '%s' if_chr_same_floor_distance_to_pad_less_than=%d remove_references_to_chr=%d backend=graph.ai.action.pad_reference+ai/ailists.tsv+pads.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_remove_references_to_chr_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI model-part actions '%s' chr_toggle_model_part=%d obj_set_model_part_visible=%d backend=graph.ai.action.model_part+ai/ailists.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_chr_toggle_model_part_node_count,
		s_ActiveScenarioGraphs.level_ai_obj_set_model_part_visible_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI object-health actions '%s' if_obj_health_less_than=%d set_obj_health=%d backend=graph.ai.action.object_health+ai/ailists.tsv+objects.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_if_obj_health_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_set_obj_health_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI special-death actions '%s' set_chr_special_death_animation=%d backend=graph.ai.action.special_death+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_chr_special_death_animation_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI room-search actions '%s' set_room_to_search=%d backend=graph.ai.action.room_search+ai/ailists.tsv+scene.glb",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_room_to_search_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI savefile flag actions '%s' set_savefile_flag=%d unset_savefile_flag=%d if_savefile_flag_set=%d if_savefile_flag_unset=%d backend=graph.ai.action.savefile_flags+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_set_savefile_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_unset_savefile_flag_node_count,
		s_ActiveScenarioGraphs.level_ai_if_savefile_flag_set_node_count,
		s_ActiveScenarioGraphs.level_ai_if_savefile_flag_unset_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI timer/countdown actions '%s' restart_timer=%d reset_timer=%d pause_timer=%d resume_timer=%d if_timer_stopped=%d if_timer_greater_than_random=%d if_timer_less_than=%d if_timer_greater_than=%d show_countdown_timer=%d hide_countdown_timer=%d set_countdown_timer=%d stop_countdown_timer=%d start_countdown_timer=%d if_countdown_timer_stopped=%d if_countdown_timer_less_than=%d if_countdown_timer_greater_than=%d backend=graph.ai.action.timer+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_restart_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_reset_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_pause_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_resume_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_if_timer_stopped_node_count,
		s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_random_node_count,
		s_ActiveScenarioGraphs.level_ai_if_timer_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_node_count,
		s_ActiveScenarioGraphs.level_ai_show_countdown_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_hide_countdown_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_set_countdown_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_stop_countdown_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_start_countdown_timer_node_count,
		s_ActiveScenarioGraphs.level_ai_if_countdown_timer_stopped_node_count,
		s_ActiveScenarioGraphs.level_ai_if_countdown_timer_less_than_node_count,
		s_ActiveScenarioGraphs.level_ai_if_countdown_timer_greater_than_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI HUD message actions '%s' show_hudmsg=%d show_hudmsg_middle=%d show_hudmsg_top_middle=%d backend=graph.ai.action.hud+ai/ailists.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_show_hudmsg_node_count,
		s_ActiveScenarioGraphs.level_ai_show_hudmsg_middle_node_count,
		s_ActiveScenarioGraphs.level_ai_show_hudmsg_top_middle_node_count);
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.GRAPH: AI vehicle motion actions '%s' hovercar_begin_path=%d set_vehicle_speed=%d set_rotor_speed=%d backend=graph.ai.action.vehicle+navigation/paths.tsv",
		s_ActiveScenarioGraphs.level_graph_path,
		s_ActiveScenarioGraphs.level_ai_hovercar_begin_path_node_count,
		s_ActiveScenarioGraphs.level_ai_set_vehicle_speed_node_count,
		s_ActiveScenarioGraphs.level_ai_set_rotor_speed_node_count);
	free(text);
	active = 1;

	if (prefer_mp) {
		return active;
	}

	mission = s_findMissionForScenario(scenario);
	if (!mission || !mission->id[0]) {
		if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
			s_graphFailure(ASSET_MISSION, scenario->id, NULL,
				"no mission catalog entry for scenario");
		}
		return active;
	}

	if (!mission->ext.mission.mission_graph_file[0]) {
		s_graphFailure(ASSET_MISSION, mission->id, NULL,
			"missing mission.graph.json binding");
		return active;
	}

	text_size = 0;
	text = s_loadGraphText(mission->ext.mission.mission_graph_file,
		&text_size);
	if (!text) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file, "load failed");
		return active;
	}

	if (!s_validateGraphText(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file, text,
			"pd2.mission.graph.v1", scenario->id)) {
		free(text);
		return active;
	}
	mission_phase_node_count = s_countTextOccurrences(text,
		"\"kind\": \"mission.phase.source\"");
	if (mission_phase_node_count < 5) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file,
			"missing executable mission phase source nodes");
		free(text);
		return active;
	}

	mission_objectives_path[0] = '\0';
	if (!s_bindMissionGraphObjectivesPath(mission, text,
			mission_objectives_path, sizeof(mission_objectives_path))) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission->ext.mission.mission_graph_file,
			"missing mission objectives table binding");
		free(text);
		return active;
	}

	mission_objectives = 0;
	mission_criteria = 0;
	if (!s_loadMissionObjectiveSourceRows(mission_objectives_path, text,
			&mission_objective_rows, &mission_objectives,
			&mission_criteria_rows, &mission_criteria)) {
		s_graphFailure(ASSET_MISSION, mission->id,
			mission_objectives_path,
			"missing executable mission objective source rows");
		free(text);
		return active;
	}

	s_copyString(s_ActiveScenarioGraphs.mission_id,
		sizeof(s_ActiveScenarioGraphs.mission_id), mission->id);
	s_copyString(s_ActiveScenarioGraphs.mission_graph_path,
		sizeof(s_ActiveScenarioGraphs.mission_graph_path),
		mission->ext.mission.mission_graph_file);
	s_copyString(s_ActiveScenarioGraphs.mission_objectives_path,
		sizeof(s_ActiveScenarioGraphs.mission_objectives_path),
		mission_objectives_path);
	s_ActiveScenarioGraphs.mission_graph_size = text_size;
	s_ActiveScenarioGraphs.mission_objectives = mission_objective_rows;
	s_ActiveScenarioGraphs.mission_objective_criteria =
		mission_criteria_rows;
	s_ActiveScenarioGraphs.mission_objective_nodes = mission_objectives;
	s_ActiveScenarioGraphs.mission_objective_criteria_nodes =
		mission_criteria;
	s_ActiveScenarioGraphs.mission_phase_node_count =
		mission_phase_node_count;
	s_ActiveScenarioGraphs.mission_graph_active = 1;
	s_ActiveScenarioGraphs.mission_objective_runtime_active = 1;
	s_ActiveScenarioGraphs.mission_stage_flags = 0;
	s_ActiveScenarioGraphs.mission_stage_flags_valid = 1;

	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: activated mission graph '%s' scenario='%s' path=%s bytes=%u backend=%s",
		mission->id, scenario->id, mission->ext.mission.mission_graph_file,
		(unsigned)text_size,
		strstr(text, "\"parity_backend\"") ? "parity" : "graph");
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: objective runtime source '%s' objectives=%d criteria=%d backend=graph.objective.source",
		mission_objectives_path, mission_objectives, mission_criteria);
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: phase source '%s' phases=%d backend=graph.mission.phase+mission.graph.nodes",
		s_ActiveScenarioGraphs.mission_graph_path,
		s_ActiveScenarioGraphs.mission_phase_node_count);
	scenarioSourceMissionGraphRecordPhase("load", "mission.graph.activate");
	free(text);

	return active;
}

s32 scenarioSourceObjectiveGraphIsActive(void)
{
	return s_ActiveScenarioGraphs.mission_objective_runtime_active;
}

static s32 s_aiGraphRuntimeFailure(const char *action, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: scenario graph '%s' cannot %s "
			"AI action from public source (%s); refusing legacy-only AI behavior.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			action && action[0] ? action : "execute",
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"SCENARIO.GRAPH: cannot %s AI action for '%s' (%s)",
		action && action[0] ? action : "execute",
		s_ActiveScenarioGraphs.scenario_id[0]
			? s_ActiveScenarioGraphs.scenario_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 1;
}

static void s_aiGraphApplyBranch(s32 branch_taken, s32 label,
	s32 false_offset);

static s32 s_aiGraphRequireListControlNode(const char *action,
	s32 node_count)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		s_aiGraphRuntimeFailure(action, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

static s32 s_aiGraphRequireBasicMotionNode(const char *action,
	s32 node_count)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphRequireAnimationNode(const char *action,
	s32 node_count)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphRequireRandomControlNode(const char *kind,
	const char *name, s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.%s.%s node", kind, name);
		return s_aiGraphRuntimeFailure(name, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(name,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphRequireDebugNoOpNode(const char *action,
	s32 node_count)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStop(struct chrdata *chr,
	struct chopperobj *hovercar)
{
	if (!s_aiGraphRequireBasicMotionNode("stop",
			s_ActiveScenarioGraphs.level_ai_stop_node_count)) {
		return 0;
	}

	if (chr) {
		chrTryStop(chr);
	} else if (hovercar) {
		chopperStop(hovercar);
	}
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_stop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action stop chr=%d hovercar=%d source=%s backend=graph.ai.action.basic_motion+ai/ailists.tsv",
			chr ? 1 : 0, hovercar ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_stop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteKneel(struct chrdata *chr)
{
	if (!s_aiGraphRequireBasicMotionNode("kneel",
			s_ActiveScenarioGraphs.level_ai_kneel_node_count)) {
		return 0;
	}

	chrTryKneel(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_kneel_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action kneel chr=%d source=%s backend=graph.ai.action.basic_motion+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_kneel_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireCharacterLifecycleNode(const char *action,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSurrender(struct chrdata *chr)
{
	if (!s_aiGraphRequireCharacterLifecycleNode("surrender",
			s_ActiveScenarioGraphs.level_ai_surrender_node_count)) {
		return 0;
	}

	chrTrySurrender(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_surrender_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action surrender chr=%d source=%s backend=graph.ai.action.character_lifecycle+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_surrender_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteFadeOut(struct chrdata *chr)
{
	if (!s_aiGraphRequireCharacterLifecycleNode("fade_out",
			s_ActiveScenarioGraphs.level_ai_fade_out_node_count)) {
		return 0;
	}

	chrFadeOut(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_fade_out_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action fade_out chr=%d source=%s backend=graph.ai.action.character_lifecycle+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_fade_out_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRemoveChr(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr;
	s32 applied;

	if (!s_aiGraphRequireCharacterLifecycleNode("remove_chr",
			s_ActiveScenarioGraphs.level_ai_remove_chr_node_count)) {
		return 0;
	}

	chr = chrFindById(basechr, chrnum);
	applied = chr && chr->prop;
	if (applied) {
		chr->hidden |= 0x20;
	}
	g_Vars.aioffset += 3;

	if (!s_ActiveScenarioGraphs.ai_action_remove_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action remove_chr chr=%d applied=%d source=%s backend=graph.ai.action.character_lifecycle+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_remove_chr_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireCombatNode(const char *kind, const char *action,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.%s.%s node", kind, action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphExecuteCombatTry(const char *action, s32 node_count,
	struct chrdata *chr, s32 label, s32 false_offset, s32 result,
	s32 *logged)
{
	if (!s_aiGraphRequireCombatNode("action", action, node_count)) {
		return 0;
	}
	s_aiGraphApplyBranch(result, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s chr=%d label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			action, chr ? 1 : 0, label, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTrySidestep(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTrySidestep(chr);
	return s_aiGraphExecuteCombatTry("try_sidestep",
		s_ActiveScenarioGraphs.level_ai_try_sidestep_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_sidestep_logged);
}

s32 scenarioSourceAiGraphExecuteTryJumpOut(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryJumpOut(chr);
	return s_aiGraphExecuteCombatTry("try_jump_out",
		s_ActiveScenarioGraphs.level_ai_try_jump_out_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_jump_out_logged);
}

s32 scenarioSourceAiGraphExecuteTryRunSideways(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryRunSideways(chr);
	return s_aiGraphExecuteCombatTry("try_run_sideways",
		s_ActiveScenarioGraphs.level_ai_try_run_sideways_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_run_sideways_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackWalk(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackWalk(chr);
	return s_aiGraphExecuteCombatTry("try_attack_walk",
		s_ActiveScenarioGraphs.level_ai_try_attack_walk_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_walk_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackRun(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackRun(chr);
	return s_aiGraphExecuteCombatTry("try_attack_run",
		s_ActiveScenarioGraphs.level_ai_try_attack_run_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_run_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackRoll(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackRoll(chr);
	return s_aiGraphExecuteCombatTry("try_attack_roll",
		s_ActiveScenarioGraphs.level_ai_try_attack_roll_node_count,
		chr, label, 3, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_roll_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackStand(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackStand(chr, thingtype, thingid);
	return s_aiGraphExecuteCombatTry("try_attack_stand",
		s_ActiveScenarioGraphs.level_ai_try_attack_stand_node_count,
		chr, label, 7, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_stand_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackKneel(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackKneel(chr, thingtype, thingid);
	return s_aiGraphExecuteCombatTry("try_attack_kneel",
		s_ActiveScenarioGraphs.level_ai_try_attack_kneel_node_count,
		chr, label, 7, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_kneel_logged);
}

s32 scenarioSourceAiGraphExecuteTryAttackLie(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryAttackLie(chr, thingtype, thingid);
	return s_aiGraphExecuteCombatTry("try_attack_lie",
		s_ActiveScenarioGraphs.level_ai_try_attack_lie_node_count,
		chr, label, 7, result,
		&s_ActiveScenarioGraphs.ai_action_try_attack_lie_logged);
}

s32 scenarioSourceAiGraphExecuteIfAttackLocked(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_aiGraphRequireCombatNode("condition", "if_attack_locked",
			s_ActiveScenarioGraphs.level_ai_if_attack_locked_node_count)) {
		return 0;
	}
	result = chr && chr->actiontype == ACT_ATTACK &&
		!chr->act_attack.reaim &&
		(chr->act_attack.flags & ATTACKFLAG_DONTTURN);
	s_aiGraphApplyBranch(result, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_attack_locked_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_attack_locked label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			label, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_attack_locked_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfAttacking(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_aiGraphRequireCombatNode("condition", "if_attacking",
			s_ActiveScenarioGraphs.level_ai_if_attacking_node_count)) {
		return 0;
	}
	result = chr && chr->actiontype == ACT_ATTACK;
	s_aiGraphApplyBranch(result, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_attacking_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_attacking label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			label, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_attacking_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryModifyAttack(struct chrdata *chr,
	struct chopperobj *hovercar, u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("action", "try_modify_attack",
			s_ActiveScenarioGraphs.level_ai_try_modify_attack_node_count)) {
		return 1;
	}
	result = (chr && chrTryModifyAttack(chr, thingtype, thingid)) ||
		(hovercar && chopperAttack(hovercar));
	s_aiGraphApplyBranch(result, label, 7);
	if (!s_ActiveScenarioGraphs.ai_action_try_modify_attack_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_modify_attack chr=%d hovercar=%d label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			chr ? 1 : 0, hovercar ? 1 : 0, label, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_try_modify_attack_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteFaceEntity(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("action", "face_entity",
			s_ActiveScenarioGraphs.level_ai_face_entity_node_count)) {
		return 1;
	}
	result = chr && chrFaceEntity(chr, thingtype, thingid);
	s_aiGraphApplyBranch(result, label, 7);
	if (!s_ActiveScenarioGraphs.ai_action_face_entity_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action face_entity chr=%d type=%u id=%u label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			chr ? 1 : 0, (unsigned)thingtype, (unsigned)thingid,
			label, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_face_entity_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteApplyGsetDamage(struct chrdata *basechr,
	s32 chrnum, s32 hitpart, const struct gset *gset)
{
	struct chrdata *chr;
	struct coord pos = { 0, 0, 0 };
	f32 damage;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("action", "apply_gset_damage",
			s_ActiveScenarioGraphs.level_ai_apply_gset_damage_node_count)) {
		return 1;
	}
	chr = chrFindById(basechr, chrnum);
	applied = chr && chr->prop && gset;
	if (applied) {
		struct gset *runtime_gset = (struct gset *)gset;
		damage = gsetGetDamage(runtime_gset);
		chrDamageByImpact(chr, damage, &pos, runtime_gset, NULL,
			(s8)hitpart);
	}
	g_Vars.aioffset += 8;
	if (!s_ActiveScenarioGraphs.ai_action_apply_gset_damage_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action apply_gset_damage chr=%d hitpart=%d applied=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			chrnum, hitpart, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_apply_gset_damage_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDamageChr(struct chrdata *basechr,
	s32 attacker_chrnum, s32 victim_chrnum, s32 hitpart)
{
	struct chrdata *chr1;
	struct chrdata *chr2;
	struct prop *prop;
	struct coord vector = { 0, 0, 0 };
	struct weaponobj *weapon;
	f32 damage;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("action", "chr_damage_chr",
			s_ActiveScenarioGraphs.level_ai_chr_damage_chr_node_count)) {
		return 1;
	}
	chr1 = chrFindById(basechr, attacker_chrnum);
	chr2 = chrFindById(basechr, victim_chrnum);
	applied = 0;
	if (chr1 && chr2 && chr1->prop && chr2->prop) {
		prop = chrGetHeldUsableProp(chr1, HAND_RIGHT);
		if (!prop) {
			prop = chrGetHeldUsableProp(chr1, HAND_LEFT);
		}
		if (prop) {
			vector.x = chr2->prop->pos.x - chr1->prop->pos.x;
			vector.y = chr2->prop->pos.y - chr1->prop->pos.y;
			vector.z = chr2->prop->pos.z - chr1->prop->pos.z;
			guNormalize(&vector.x, &vector.y, &vector.z);
			weapon = prop->weapon;
			damage = gsetGetDamage(&weapon->gset);
			chrDamageByImpact(chr2, damage, &vector, &weapon->gset,
				chr1->prop, (s8)hitpart);
			applied = 1;
		}
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_chr_damage_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_damage_chr attacker=%d victim=%d hitpart=%d applied=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			attacker_chrnum, victim_chrnum, hitpart, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_damage_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteConsiderGrenadeThrow(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("condition", "consider_grenade_throw",
			s_ActiveScenarioGraphs.level_ai_consider_grenade_throw_node_count)) {
		return 1;
	}
	result = chr && chrConsiderGrenadeThrow(chr, thingtype, thingid);
	s_aiGraphApplyBranch(result, label, 7);
	if (!s_ActiveScenarioGraphs.ai_condition_consider_grenade_throw_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition consider_grenade_throw chr=%d type=%u id=%u label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			chr ? 1 : 0, (unsigned)thingtype, (unsigned)thingid,
			label, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_consider_grenade_throw_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDropItem(struct chrdata *chr,
	u32 modelnum, u32 weaponnum, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!s_aiGraphRequireCombatNode("action", "drop_item",
			s_ActiveScenarioGraphs.level_ai_drop_item_node_count)) {
		return 1;
	}
	result = chr && chrDropItem(chr, modelnum, weaponnum);
	s_aiGraphApplyBranch(result, label, 6);
	if (!s_ActiveScenarioGraphs.ai_action_drop_item_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action drop_item chr=%d model=%u weapon=%u label=%d result=%d source=%s backend=graph.ai.action.combat+ai/ailists.tsv",
			chr ? 1 : 0, (unsigned)modelnum, (unsigned)weaponnum,
			label, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_drop_item_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireTargetMovementNode(const char *action,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphExecuteTargetMovementTry(const char *action,
	s32 node_count, struct chrdata *chr, s32 label, s32 false_offset,
	s32 result, s32 chrnum, s32 *logged)
{
	s32 required;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequireTargetMovementNode(action, node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	s_aiGraphApplyBranch(result, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s chr=%d target_chr=%d label=%d result=%d source=%s backend=graph.ai.action.target_movement+ai/ailists.tsv",
			action, chr ? 1 : 0, chrnum, label, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryRunFromTarget(struct chrdata *chr,
	s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrTryRunFromTarget(chr);
	return s_aiGraphExecuteTargetMovementTry("try_run_from_target",
		s_ActiveScenarioGraphs.level_ai_try_run_from_target_node_count,
		chr, label, 3, result, -1,
		&s_ActiveScenarioGraphs.ai_action_try_run_from_target_logged);
}

static s32 s_aiGraphExecuteTryToTargetProp(const char *action,
	s32 node_count, struct chrdata *chr, s32 label, u32 speed,
	s32 *logged)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrGoToTarget(chr, speed);
	return s_aiGraphExecuteTargetMovementTry(action, node_count,
		chr, label, 3, result, -1, logged);
}

s32 scenarioSourceAiGraphExecuteTryJogToTargetProp(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteTryToTargetProp("try_jog_to_target_prop",
		s_ActiveScenarioGraphs.level_ai_try_jog_to_target_prop_node_count,
		chr, label, GOPOSFLAG_JOG,
		&s_ActiveScenarioGraphs.ai_action_try_jog_to_target_prop_logged);
}

s32 scenarioSourceAiGraphExecuteTryWalkToTargetProp(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteTryToTargetProp("try_walk_to_target_prop",
		s_ActiveScenarioGraphs.level_ai_try_walk_to_target_prop_node_count,
		chr, label, GOPOSFLAG_WALK,
		&s_ActiveScenarioGraphs.ai_action_try_walk_to_target_prop_logged);
}

s32 scenarioSourceAiGraphExecuteTryRunToTargetProp(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteTryToTargetProp("try_run_to_target_prop",
		s_ActiveScenarioGraphs.level_ai_try_run_to_target_prop_node_count,
		chr, label, GOPOSFLAG_RUN,
		&s_ActiveScenarioGraphs.ai_action_try_run_to_target_prop_logged);
}

s32 scenarioSourceAiGraphExecuteTryGoToCoverProp(struct chrdata *chr,
	s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrGoToCoverProp(chr);
	return s_aiGraphExecuteTargetMovementTry("try_go_to_cover_prop",
		s_ActiveScenarioGraphs.level_ai_try_go_to_cover_prop_node_count,
		chr, label, 3, result, -1,
		&s_ActiveScenarioGraphs.ai_action_try_go_to_cover_prop_logged);
}

static s32 s_aiGraphExecuteTryToChr(const char *action, s32 node_count,
	struct chrdata *chr, s32 chrnum, s32 label, u32 speed, s32 *logged)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	result = chr && chrGoToChr(chr, chrnum, speed);
	return s_aiGraphExecuteTargetMovementTry(action, node_count,
		chr, label, 4, result, chrnum, logged);
}

s32 scenarioSourceAiGraphExecuteTryJogToChr(struct chrdata *chr,
	s32 chrnum, s32 label)
{
	return s_aiGraphExecuteTryToChr("try_jog_to_chr",
		s_ActiveScenarioGraphs.level_ai_try_jog_to_chr_node_count,
		chr, chrnum, label, GOPOSFLAG_JOG,
		&s_ActiveScenarioGraphs.ai_action_try_jog_to_chr_logged);
}

s32 scenarioSourceAiGraphExecuteTryWalkToChr(struct chrdata *chr,
	s32 chrnum, s32 label)
{
	return s_aiGraphExecuteTryToChr("try_walk_to_chr",
		s_ActiveScenarioGraphs.level_ai_try_walk_to_chr_node_count,
		chr, chrnum, label, GOPOSFLAG_WALK,
		&s_ActiveScenarioGraphs.ai_action_try_walk_to_chr_logged);
}

s32 scenarioSourceAiGraphExecuteTryRunToChr(struct chrdata *chr,
	s32 chrnum, s32 label)
{
	return s_aiGraphExecuteTryToChr("try_run_to_chr",
		s_ActiveScenarioGraphs.level_ai_try_run_to_chr_node_count,
		chr, chrnum, label, GOPOSFLAG_RUN,
		&s_ActiveScenarioGraphs.ai_action_try_run_to_chr_logged);
}

static s32 s_aiGraphRequirePerceptionAlarmNode(const char *kind,
	const char *name, s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.%s.%s node", kind, name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

static s32 s_aiGraphExecutePerceptionAlarmBranch(const char *kind,
	const char *name, s32 node_count, s32 label, s32 false_offset,
	s32 branch_taken, s32 value, s32 *logged)
{
	s32 required;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequirePerceptionAlarmNode(kind, name, node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	s_aiGraphApplyBranch(branch_taken, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI %s %s value=%d label=%d result=%d source=%s backend=graph.ai.condition.perception_alarm+ai/ailists.tsv",
			kind, name, value, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCanHearAlarm(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_can_hear_alarm",
		s_ActiveScenarioGraphs.level_ai_if_can_hear_alarm_node_count,
		label, 3, chrCanHearAlarm(chr), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_can_hear_alarm_logged);
}

s32 scenarioSourceAiGraphExecuteIfPatrolling(struct chrdata *chr, s32 label)
{
	s32 branch_taken = chr &&
		(chr->actiontype == ACT_PATROL ||
			(chr->actiontype == ACT_GOPOS &&
				(chr->act_gopos.flags & GOPOSFLAG_FORPATHSTART)));

	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_patrolling",
		s_ActiveScenarioGraphs.level_ai_if_patrolling_node_count,
		label, 3, branch_taken, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_patrolling_logged);
}

s32 scenarioSourceAiGraphExecuteIfAlarmActive(s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_alarm_active",
		s_ActiveScenarioGraphs.level_ai_if_alarm_active_node_count,
		label, 3, alarmIsActive(), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_alarm_active_logged);
}

s32 scenarioSourceAiGraphExecuteIfGasActive(s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_gas_active",
		s_ActiveScenarioGraphs.level_ai_if_gas_active_node_count,
		label, 3, gasIsActive(), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_gas_active_logged);
}

s32 scenarioSourceAiGraphExecuteIfHearsTarget(struct chrdata *chr, s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_hears_target",
		s_ActiveScenarioGraphs.level_ai_if_hears_target_node_count,
		label, 3, chr && chrIsHearingTarget(chr), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_hears_target_logged);
}

s32 scenarioSourceAiGraphExecuteIfSawInjury(struct chrdata *chr,
	s32 value, s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_saw_injury",
		s_ActiveScenarioGraphs.level_ai_if_saw_injury_node_count,
		label, 4, chr && chrSawInjury(chr, (u8)value), value,
		&s_ActiveScenarioGraphs.ai_condition_if_saw_injury_logged);
}

s32 scenarioSourceAiGraphExecuteIfSawDeath(struct chrdata *chr,
	s32 value, s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_saw_death",
		s_ActiveScenarioGraphs.level_ai_if_saw_death_node_count,
		label, 4, chr && chrSawDeath(chr, (u8)value), value,
		&s_ActiveScenarioGraphs.ai_condition_if_saw_death_logged);
}

s32 scenarioSourceAiGraphExecuteIfLosToTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 label)
{
	s32 branch_taken = (chr && chrHasLosToTarget(chr)) ||
		(hovercar && chopperCheckTargetInFov(hovercar, 64) &&
			chopperCheckTargetInSight(hovercar));

	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_los_to_target",
		s_ActiveScenarioGraphs.level_ai_if_los_to_target_node_count,
		label, 3, branch_taken, hovercar ? 1 : 0,
		&s_ActiveScenarioGraphs.ai_condition_if_los_to_target_logged);
}

s32 scenarioSourceAiGraphExecuteIfLosToAttackTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 label)
{
	s32 branch_taken = (chr && chr->prop &&
			chrHasLosToAttackTarget(chr, &chr->prop->pos,
				chr->prop->rooms, true)) ||
		(hovercar && chopperCheckTargetInFov(hovercar, 64) &&
			chopperCheckTargetInSight(hovercar));

	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_los_to_attack_target",
		s_ActiveScenarioGraphs.level_ai_if_los_to_attack_target_node_count,
		label, 3, branch_taken, hovercar ? 1 : 0,
		&s_ActiveScenarioGraphs.ai_condition_if_los_to_attack_target_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetNearlyInSight(struct chrdata *chr,
	u32 distance, s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_target_nearly_in_sight",
		s_ActiveScenarioGraphs.level_ai_if_target_nearly_in_sight_node_count,
		label, 7, chr && chrIsTargetNearlyInSight(chr, distance),
		(s32)distance,
		&s_ActiveScenarioGraphs.ai_condition_if_target_nearly_in_sight_logged);
}

s32 scenarioSourceAiGraphExecuteIfNearlyInTargetsSight(struct chrdata *chr,
	u32 distance, s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_nearly_in_targets_sight",
		s_ActiveScenarioGraphs.level_ai_if_nearly_in_targets_sight_node_count,
		label, 7, chr && chrIsNearlyInTargetsSight(chr, distance),
		(s32)distance,
		&s_ActiveScenarioGraphs.ai_condition_if_nearly_in_targets_sight_logged);
}

s32 scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget(
	struct chrdata *chr, s32 label)
{
	s32 required;
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequirePerceptionAlarmNode("action",
		"set_pad_preset_to_pad_on_route_to_target",
		s_ActiveScenarioGraphs
			.level_ai_set_pad_preset_to_pad_on_route_to_target_node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.pads_path[0] ||
			!s_ActiveScenarioGraphs.paths_path[0]) {
		s_aiGraphRuntimeFailure("set_pad_preset_to_pad_on_route_to_target",
			"missing pads.tsv or navigation/paths.tsv source");
		return 1;
	}
	branch_taken = chr && chr->prop && chrGetTargetProp(chr) &&
		chrSetPadPresetToPadOnRouteToTarget(chr);
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_pad_on_route_to_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_pad_preset_to_pad_on_route_to_target label=%d result=%d source=%s pads=%s paths=%s backend=graph.ai.condition.perception_alarm+ai/ailists.tsv+pads.tsv+navigation/paths.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path,
			s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_pad_on_route_to_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSawTargetRecently(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_saw_target_recently",
		s_ActiveScenarioGraphs.level_ai_if_saw_target_recently_node_count,
		label, 3, chr && chrSawTargetRecently(chr), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_saw_target_recently_logged);
}

s32 scenarioSourceAiGraphExecuteIfHeardTargetRecently(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecutePerceptionAlarmBranch("condition",
		"if_heard_target_recently",
		s_ActiveScenarioGraphs.level_ai_if_heard_target_recently_node_count,
		label, 3, chr && chrHeardTargetRecently(chr), 0,
		&s_ActiveScenarioGraphs.ai_condition_if_heard_target_recently_logged);
}

static s32 s_aiGraphExecuteSpatialPerceptionBranch(const char *name,
	s32 node_count, s32 label, s32 false_offset, s32 branch_taken,
	s32 value, s32 require_pads, s32 require_objects, s32 *logged)
{
	s32 required;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequirePerceptionAlarmNode("condition", name,
		node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	if (require_pads && !s_ActiveScenarioGraphs.pads_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing pads.tsv source");
		return 1;
	}
	if (require_objects && !s_ActiveScenarioGraphs.objects_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing objects.tsv source");
		return 1;
	}
	s_aiGraphApplyBranch(branch_taken, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition %s value=%d label=%d result=%d source=%s backend=graph.ai.condition.spatial_perception+ai/ailists.tsv",
			name, value, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

static s32 s_aiGraphExecuteDistancePerceptionBranch(const char *name,
	s32 node_count, s32 label, s32 false_offset, s32 branch_taken,
	s32 value, s32 require_pads, s32 *logged)
{
	s32 required;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequirePerceptionAlarmNode("condition", name,
		node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	if (require_pads && !s_ActiveScenarioGraphs.pads_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing pads.tsv source");
		return 1;
	}
	s_aiGraphApplyBranch(branch_taken, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition %s value=%d label=%d result=%d source=%s backend=graph.ai.condition.distance_perception+ai/ailists.tsv",
			name, value, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

static s32 s_aiGraphExecuteRoomObjectWeaponBranch(const char *name,
	s32 node_count, s32 label, s32 false_offset, s32 branch_taken,
	s32 value, s32 require_pads, s32 require_objects, s32 *logged)
{
	s32 required;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	required = s_aiGraphRequirePerceptionAlarmNode("condition", name,
		node_count);
	if (required < 0) {
		return 1;
	}
	if (!required) {
		return 0;
	}
	if (require_pads && !s_ActiveScenarioGraphs.pads_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing pads.tsv source");
		return 1;
	}
	if (require_objects && !s_ActiveScenarioGraphs.objects_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing objects.tsv source");
		return 1;
	}
	s_aiGraphApplyBranch(branch_taken, label, false_offset);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition %s value=%d label=%d result=%d source=%s backend=graph.ai.condition.room_object_weapon+ai/ailists.tsv",
			name, value, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

static s32 s_aiGraphChrInOnScreenRoom(struct chrdata *chr)
{
	s32 i;

	if (!chr || !chr->prop) {
		return 0;
	}
	for (i = 0; chr->prop->rooms[i] != -1; i++) {
		if (bgRoomIsOnscreen(chr->prop->rooms[i])) {
			return 1;
		}
	}
	return 0;
}

static s32 s_aiGraphChrSeesSuspiciousItem(struct chrdata *chr)
{
	s16 propnums[256];
	s16 *ptr;

	if (!chr || !chr->prop) {
		return 0;
	}
	roomGetProps(chr->prop->rooms, &propnums[0], 256);
	ptr = &propnums[0];
	while (*ptr >= 0) {
		struct prop *prop = &g_Vars.props[*ptr];
		struct defaultobj *obj = prop->obj;

		if (prop->type == PROPTYPE_WEAPON) {
			if (obj && (obj->hidden & OBJHFLAG_SUSPICIOUS) &&
					chrHasLosToProp(chr, prop)) {
				return 1;
			}
		} else if (prop->type == PROPTYPE_OBJ) {
			if (obj && ((obj->hidden & OBJHFLAG_SUSPICIOUS) ||
					!objIsHealthy(obj)) &&
					chrHasLosToProp(chr, prop)) {
				return 1;
			}
		} else if (prop->type == PROPTYPE_EXPLOSION) {
			if (chrHasLosToProp(chr, prop)) {
				return 1;
			}
		}
		ptr++;
	}
	return 0;
}

s32 scenarioSourceAiGraphExecuteIfLosToChr(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr = chrFindById(basechr, chrnum);
	s32 branch_taken = chr && chr->prop &&
		chrHasLosToPos(basechr, &chr->prop->pos, chr->prop->rooms);

	return s_aiGraphExecuteSpatialPerceptionBranch("if_los_to_chr",
		s_ActiveScenarioGraphs.level_ai_if_los_to_chr_node_count,
		label, 4, branch_taken, chrnum, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_los_to_chr_logged);
}

s32 scenarioSourceAiGraphExecuteIfNeverBeenOnScreen(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch("if_never_been_on_screen",
		s_ActiveScenarioGraphs.level_ai_if_never_been_on_screen_node_count,
		label, 3, chr && ((chr->chrflags & CHRCFLAG_EVERONSCREEN) == 0),
		0, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_never_been_on_screen_logged);
}

s32 scenarioSourceAiGraphExecuteIfOnScreen(struct chrdata *chr, s32 label)
{
	s32 branch_taken = chr && chr->prop &&
		(chr->prop->flags & (PROPFLAG_ONTHISSCREENTHISTICK |
			PROPFLAG_ONANYSCREENTHISTICK |
			PROPFLAG_ONANYSCREENPREVTICK));

	return s_aiGraphExecuteSpatialPerceptionBranch("if_on_screen",
		s_ActiveScenarioGraphs.level_ai_if_on_screen_node_count,
		label, 3, branch_taken, 0, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_on_screen_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrInOnScreenRoom(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr = chrFindById(basechr, chrnum);

	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_chr_in_on_screen_room",
		s_ActiveScenarioGraphs.level_ai_if_chr_in_on_screen_room_node_count,
		label, 4, s_aiGraphChrInOnScreenRoom(chr), chrnum, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_chr_in_on_screen_room_logged);
}

s32 scenarioSourceAiGraphExecuteIfRoomIsOnScreen(struct chrdata *chr,
	s32 pad, s32 label)
{
	s32 room_id = chrGetPadRoom(chr, pad);

	return s_aiGraphExecuteSpatialPerceptionBranch("if_room_is_on_screen",
		s_ActiveScenarioGraphs.level_ai_if_room_is_on_screen_node_count,
		label, 5, room_id >= 0 && bgRoomIsOnscreen(room_id), pad, 1, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_room_is_on_screen_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetAimingAtMe(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch("if_target_aiming_at_me",
		s_ActiveScenarioGraphs.level_ai_if_target_aiming_at_me_node_count,
		label, 3, chr && chrIsTargetAimingAtMe(chr), 0, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_target_aiming_at_me_logged);
}

s32 scenarioSourceAiGraphExecuteIfNearMiss(struct chrdata *chr, s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch("if_near_miss",
		s_ActiveScenarioGraphs.level_ai_if_near_miss_node_count,
		label, 3, chr && chrResetNearMiss(chr), 0, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_near_miss_logged);
}

s32 scenarioSourceAiGraphExecuteIfSeesSuspiciousItem(struct chrdata *chr,
	s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_sees_suspicious_item",
		s_ActiveScenarioGraphs.level_ai_if_sees_suspicious_item_node_count,
		label, 3, s_aiGraphChrSeesSuspiciousItem(chr), 0, 0, 1,
		&s_ActiveScenarioGraphs.ai_condition_if_sees_suspicious_item_logged);
}

s32 scenarioSourceAiGraphExecuteIfCheckFovWithTarget(struct chrdata *chr,
	s32 angle, s32 use_x, s32 invert_yvisang, s32 label)
{
	s32 branch_taken;

	if (!invert_yvisang) {
		if (use_x) {
			branch_taken = chr && chrIsInTargetsFovX(chr, angle);
		} else {
			branch_taken = chr && chrIsVerticalAngleToTargetWithin(chr,
				angle);
		}
	} else {
		branch_taken = chr && chr->yvisang &&
			chrIsVerticalAngleToTargetWithin(chr, chr->yvisang) == 0;
	}
	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_check_fov_with_target",
		s_ActiveScenarioGraphs.level_ai_if_check_fov_with_target_node_count,
		label, 6, branch_taken, angle, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_check_fov_with_target_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetInFovLeft(struct chrdata *chr,
	s32 angle, s32 label)
{
	s32 branch_taken = chr &&
		chrGetAngleToTarget(chr) < angle * M_BADTAU * 0.00390625f;

	return s_aiGraphExecuteSpatialPerceptionBranch("if_target_in_fov_left",
		s_ActiveScenarioGraphs.level_ai_if_target_in_fov_left_node_count,
		label, 4, branch_taken, angle, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_target_in_fov_left_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft(struct chrdata *chr,
	s32 angle, s32 label)
{
	s32 branch_taken = chr &&
		chrGetAngleToTarget(chr) > angle * M_BADTAU * 0.00390625f;

	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_target_out_of_fov_left",
		s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_left_node_count,
		label, 4, branch_taken, angle, 0, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_target_out_of_fov_left_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetInFov(struct chrdata *chr,
	s32 angle, s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch("if_target_in_fov",
		s_ActiveScenarioGraphs.level_ai_if_target_in_fov_node_count,
		label, 4, chr && chrIsTargetInFov(chr, angle, 0), angle, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_target_in_fov_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetOutOfFov(struct chrdata *chr,
	s32 angle, s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch("if_target_out_of_fov",
		s_ActiveScenarioGraphs.level_ai_if_target_out_of_fov_node_count,
		label, 4, chr && !chrIsTargetInFov(chr, angle, 0), angle, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_target_out_of_fov_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan(
	struct chrdata *chr, f32 distance, s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_distance_to_target_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_to_target_less_than_node_count,
		label, 5, chr && chrGetDistanceToTarget(chr) < distance,
		(s32)distance, 0, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_to_target_less_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan(
	struct chrdata *chr, f32 distance, s32 label)
{
	return s_aiGraphExecuteSpatialPerceptionBranch(
		"if_distance_to_target_greater_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_to_target_greater_than_node_count,
		label, 5, chr && chrGetDistanceToTarget(chr) > distance,
		(s32)distance, 0, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_to_target_greater_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan(
	struct chrdata *basechr, s32 chrnum, f32 distance, s32 padnum, s32 label)
{
	struct chrdata *chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	s32 branch = chr && padnum < 9000 &&
		chrGetDistanceToPad(chr, padnum) < distance;

	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_chr_distance_to_pad_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_chr_distance_to_pad_less_than_node_count,
		label, 8, branch, chrnum, 1,
		&s_ActiveScenarioGraphs
			.ai_condition_if_chr_distance_to_pad_less_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan(
	struct chrdata *basechr, s32 chrnum, f32 distance, s32 padnum, s32 label)
{
	struct chrdata *chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	s32 branch = chr && padnum < 9000 &&
		chrGetDistanceToPad(chr, padnum) > distance;

	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_chr_distance_to_pad_greater_than",
		s_ActiveScenarioGraphs
			.level_ai_if_chr_distance_to_pad_greater_than_node_count,
		label, 8, branch, chrnum, 1,
		&s_ActiveScenarioGraphs
			.ai_condition_if_chr_distance_to_pad_greater_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceToChrLessThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 label)
{
	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_distance_to_chr_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_to_chr_less_than_node_count,
		label, 6, chr && chrGetDistanceToChr(chr, chrnum) < distance,
		chrnum, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_to_chr_less_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 label)
{
	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_distance_to_chr_greater_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_to_chr_greater_than_node_count,
		label, 6, chr && chrGetDistanceToChr(chr, chrnum) > distance,
		chrnum, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_to_chr_greater_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfAnyChrNearSelf(struct chrdata *chr,
	f32 distance, s32 label)
{
	return s_aiGraphExecuteDistancePerceptionBranch("if_any_chr_near_self",
		s_ActiveScenarioGraphs.level_ai_if_any_chr_near_self_node_count,
		label, 5, chr && chrSetChrPresetToAnyChrNearSelf(chr, distance),
		(s32)distance, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_any_chr_near_self_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan(
	struct chrdata *chr, f32 distance, s32 padnum, s32 label)
{
	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_distance_from_target_to_pad_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_from_target_to_pad_less_than_node_count,
		label, 7, chr && chrGetDistanceFromTargetToPad(chr, padnum) < distance,
		padnum, 1,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_from_target_to_pad_less_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan(
	struct chrdata *chr, f32 distance, s32 padnum, s32 label)
{
	return s_aiGraphExecuteDistancePerceptionBranch(
		"if_distance_from_target_to_pad_greater_than",
		s_ActiveScenarioGraphs
			.level_ai_if_distance_from_target_to_pad_greater_than_node_count,
		label, 7, chr && chrGetDistanceFromTargetToPad(chr, padnum) > distance,
		padnum, 1,
		&s_ActiveScenarioGraphs
			.ai_condition_if_distance_from_target_to_pad_greater_than_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrInRoom(struct chrdata *basechr,
	s32 chrnum, s32 room_type, s32 padnum, s32 label)
{
	struct chrdata *chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	s32 room = chrGetPadRoom(basechr, padnum);
	s32 branch = 0;

	if (room_type == 0) {
		branch = room >= 0 && chr && chr->prop && chr->prop->rooms[0] == room;
	} else if (room_type == 1) {
		branch = basechr && chr && chr->prop &&
			chr->prop->rooms[0] == basechr->roomtosearch;
	} else if (room_type == 2 &&
			stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING) {
		s32 i;
		for (i = 0; i < PLAYERCOUNT(); i++) {
			if (g_Vars.players[i] && g_Vars.players[i]->eyespy &&
					g_Vars.players[i]->eyespy->prop &&
					chrGetDistanceToPad(
						g_Vars.players[i]->eyespy->prop->chr,
						padnum) < 150.0f) {
				branch = 1;
				break;
			}
		}
	}
	return s_aiGraphExecuteRoomObjectWeaponBranch("if_chr_in_room",
		s_ActiveScenarioGraphs.level_ai_if_chr_in_room_node_count,
		label, 7, branch, chrnum, 1, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_chr_in_room_logged);
}

s32 scenarioSourceAiGraphExecuteIfTargetInRoom(struct chrdata *chr,
	s32 padnum, s32 label)
{
	struct prop *prop = chrGetTargetProp(chr);
	s32 room = chrGetPadRoom(chr, padnum);
	s32 branch = room >= 0 && prop && room == prop->rooms[0];

	return s_aiGraphExecuteRoomObjectWeaponBranch("if_target_in_room",
		s_ActiveScenarioGraphs.level_ai_if_target_in_room_node_count,
		label, 5, branch, padnum, 1, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_target_in_room_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrHasObject(struct chrdata *basechr,
	s32 chrnum, s32 tag_id, s32 label)
{
	struct defaultobj *obj = objFindByTagId(tag_id);
	struct chrdata *chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	s32 branch = 0;

	if (obj && obj->prop && chr && chr->prop &&
			chr->prop->type == PROPTYPE_PLAYER) {
		s32 prevplayernum = g_Vars.currentplayernum;
		setCurrentPlayerNum(playermgrGetPlayerNumByProp(chr->prop));
		branch = invHasProp(obj->prop);
		setCurrentPlayerNum(prevplayernum);
	}
	return s_aiGraphExecuteRoomObjectWeaponBranch("if_chr_has_object",
		s_ActiveScenarioGraphs.level_ai_if_chr_has_object_node_count,
		label, 5, branch, tag_id, 0, 1,
		&s_ActiveScenarioGraphs.ai_condition_if_chr_has_object_logged);
}

s32 scenarioSourceAiGraphExecuteIfWeaponThrown(s32 weaponnum, s32 label)
{
	return s_aiGraphExecuteRoomObjectWeaponBranch("if_weapon_thrown",
		s_ActiveScenarioGraphs.level_ai_if_weapon_thrown_node_count,
		label, 4, weaponFindLanded(weaponnum) ? 1 : 0, weaponnum, 0, 0,
		&s_ActiveScenarioGraphs.ai_condition_if_weapon_thrown_logged);
}

s32 scenarioSourceAiGraphExecuteIfWeaponThrownOnObject(s32 weaponnum,
	s32 tag_id, s32 label)
{
	struct defaultobj *obj = objFindByTagId(tag_id);
	s32 branch = 0;

	if (obj && obj->prop) {
		struct prop *prop = obj->prop->child;

		while (prop) {
			if (prop->type == PROPTYPE_WEAPON &&
					prop->weapon &&
					prop->weapon->weaponnum == weaponnum) {
				branch = 1;
				break;
			}
			prop = prop->next;
		}
	}
	return s_aiGraphExecuteRoomObjectWeaponBranch(
		"if_weapon_thrown_on_object",
		s_ActiveScenarioGraphs
			.level_ai_if_weapon_thrown_on_object_node_count,
		label, 5, branch, tag_id, 0, 1,
		&s_ActiveScenarioGraphs
			.ai_condition_if_weapon_thrown_on_object_logged);
}

s32 scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped(
	struct chrdata *basechr, s32 chrnum, s32 weaponnum, s32 label)
{
	struct chrdata *chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	s32 branch = 0;

	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		setCurrentPlayerNum(playermgrGetPlayerNumByProp(chr->prop));
		branch = bgunGetWeaponNum(HAND_RIGHT) == weaponnum;
		setCurrentPlayerNum(prevplayernum);
	}
	return s_aiGraphExecuteRoomObjectWeaponBranch(
		"if_chr_has_weapon_equipped",
		s_ActiveScenarioGraphs
			.level_ai_if_chr_has_weapon_equipped_node_count,
		label, 5, branch, weaponnum, 0, 0,
		&s_ActiveScenarioGraphs
			.ai_condition_if_chr_has_weapon_equipped_logged);
}

s32 scenarioSourceAiGraphExecuteIfGunUnclaimed(struct chrdata *chr,
	s32 tag_id, s32 mode, s32 label)
{
	s32 branch = 0;

	if (mode == 0) {
		struct defaultobj *obj = objFindByTagId(tag_id);
		branch = obj && obj->prop;
	} else if (chr) {
		struct prop *prop = chr->gunprop;

		if (prop && prop->weapon && prop->parent == NULL &&
				prop->type == PROPTYPE_WEAPON) {
			struct weaponobj *weapon = prop->weapon;
			if (weapon->base.prop) {
				weapon->base.flags |= OBJFLAG_FORCENOBOUNCE;
				branch = 1;
			}
		}
	}
	return s_aiGraphExecuteRoomObjectWeaponBranch("if_gun_unclaimed",
		s_ActiveScenarioGraphs.level_ai_if_gun_unclaimed_node_count,
		label, 5, branch, tag_id, 0, mode == 0,
		&s_ActiveScenarioGraphs.ai_condition_if_gun_unclaimed_logged);
}

s32 scenarioSourceAiGraphExecuteIfObjectHealthy(s32 tag_id, s32 label)
{
	struct defaultobj *obj = objFindByTagId(tag_id);
	s32 branch = obj && obj->prop && objIsHealthy(obj);

	return s_aiGraphExecuteRoomObjectWeaponBranch("if_object_healthy",
		s_ActiveScenarioGraphs.level_ai_if_object_healthy_node_count,
		label, 4, branch, tag_id, 0, 1,
		&s_ActiveScenarioGraphs.ai_condition_if_object_healthy_logged);
}

static s32 s_aiGraphRequireObjectInteractionNode(const char *action,
	s32 node_count, s32 require_pads)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		if (strncmp(action, "if_", 3) == 0) {
			snprintf(reason, sizeof(reason),
				"missing scenario.ai.condition.%s node", action);
		}
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0] ||
			(require_pads && !s_ActiveScenarioGraphs.pads_path[0])) {
		return s_aiGraphRuntimeFailure(action,
			require_pads
				? "missing ai/ailists.tsv, objects.tsv, or pads.tsv source"
				: "missing ai/ailists.tsv or objects.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrActivatedObject(s32 chrnum,
	s32 tag_id, s32 label)
{
	struct defaultobj *obj;
	s32 branch = 0;

	if (!s_aiGraphRequireObjectInteractionNode("if_chr_activated_object",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_activated_object_node_count, 0)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		if (chrnum == CHR_ANY) {
			if (obj->hidden & (OBJHFLAG_ACTIVATED_BY_BOND |
					OBJHFLAG_ACTIVATED_BY_COOP)) {
				branch = 1;
				obj->hidden &= ~(OBJHFLAG_ACTIVATED_BY_BOND |
					OBJHFLAG_ACTIVATED_BY_COOP);
			}
		} else {
			struct chrdata *chr = chrFindById(g_Vars.chrdata, chrnum);

			if (chr && chr->prop) {
				if (chr->prop == g_Vars.bond->prop &&
						(obj->hidden & OBJHFLAG_ACTIVATED_BY_BOND)) {
					branch = 1;
					obj->hidden &= ~OBJHFLAG_ACTIVATED_BY_BOND;
				} else if (g_Vars.coopplayernum >= 0 &&
						chr->prop == g_Vars.coop->prop &&
						(obj->hidden & OBJHFLAG_ACTIVATED_BY_COOP)) {
					branch = 1;
					obj->hidden &= ~OBJHFLAG_ACTIVATED_BY_COOP;
				}
			}
		}
	}
	s_aiGraphApplyBranch(branch, label, 5);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_activated_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_activated_object chr=%d tag=%d result=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			chrnum, tag_id, branch,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_activated_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteObjInteract(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("obj_interact",
			s_ActiveScenarioGraphs.level_ai_obj_interact_node_count, 0)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		if (obj->prop->type == PROPTYPE_DOOR) {
			doorsActivate(obj->prop, false);
			applied = 1;
		} else if (obj->prop->type == PROPTYPE_OBJ ||
				obj->prop->type == PROPTYPE_WEAPON) {
			propobjInteract(obj->prop);
			applied = 1;
		}
	}
	if (!s_ActiveScenarioGraphs.ai_action_obj_interact_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action obj_interact tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_obj_interact_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDestroyObject(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("destroy_object",
			s_ActiveScenarioGraphs.level_ai_destroy_object_node_count, 0)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && objGetDestroyedLevel(obj) == 0) {
		struct defaultobj *entity = obj->prop->obj;

		if (entity->modelnum == MODEL_ELVIS_SAUCER) {
			obj->flags = (obj->flags & ~OBJFLAG_FORCEMORTAL) |
				OBJFLAG_INVINCIBLE;
			explosionCreateSimple(entity->prop, &entity->prop->pos,
				entity->prop->rooms, EXPLOSIONTYPE_LAPTOP, 0);
			smokeCreateAtProp(entity->prop, SMOKETYPE_UFO);
		} else {
			f32 damage = ((obj->maxdamage - obj->damage) + 1) /
				250.0f;
			objDamage(obj, damage, &obj->prop->pos,
				WEAPON_REMOTEMINE, -1);
		}
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_destroy_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action destroy_object tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_destroy_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDropObjectFromChr(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("drop_object_from_chr",
			s_ActiveScenarioGraphs
				.level_ai_drop_object_from_chr_node_count, 0)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->parent &&
			obj->prop->parent->type == PROPTYPE_CHR) {
		struct chrdata *chr = obj->prop->parent->chr;
		objSetDropped(obj->prop, DROPTYPE_SURRENDER);
		chr->hidden |= CHRHFLAG_DROPPINGITEM;
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_drop_object_from_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action drop_object_from_chr tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_drop_object_from_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDropItems(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("chr_drop_items",
			s_ActiveScenarioGraphs.level_ai_chr_drop_items_node_count, 0)) {
		return 0;
	}
	chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	if (chr && chr->prop) {
		chrDropConcealedItems(chr);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_drop_items_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_drop_items chr=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_chr_drop_items_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDropWeapon(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("chr_drop_weapon",
			s_ActiveScenarioGraphs.level_ai_chr_drop_weapon_node_count, 0)) {
		return 0;
	}
	chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		u32 weaponnum;
		setCurrentPlayerNum(playernum);
		weaponnum = bgunGetWeaponNum(HAND_RIGHT);
		invRemoveItemByNum(weaponnum);
		bgunCycleBack();
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	} else if (chr && chr->prop) {
		if (chr->weapons_held[0]) {
			objSetDropped(chr->weapons_held[0], DROPTYPE_DEFAULT);
			chr->hidden |= CHRHFLAG_DROPPINGITEM;
			applied = 1;
		}
		if (chr->weapons_held[1]) {
			objSetDropped(chr->weapons_held[1], DROPTYPE_DEFAULT);
			chr->hidden |= CHRHFLAG_DROPPINGITEM;
			applied = 1;
		}
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_drop_weapon_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_drop_weapon chr=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_chr_drop_weapon_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteGiveObjectToChr(struct chrdata *basechr,
	s32 tag_id, s32 chrnum)
{
	struct defaultobj *obj;
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("give_object_to_chr",
			s_ActiveScenarioGraphs.level_ai_give_object_to_chr_node_count, 0)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	chr = basechr ? chrFindById(basechr, chrnum) : NULL;
	if (obj && obj->prop && chr && chr->prop) {
		if (chr->prop->type == PROPTYPE_PLAYER) {
			u32 something;
			u32 prevplayernum = g_Vars.currentplayernum;
			struct defaultobj *obj2 = obj->prop->obj;
			u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
			setCurrentPlayerNum(playernum);

#if VERSION >= VERSION_NTSC_1_0
			if (obj->prop->parent) {
				objDetach(obj->prop);
				objFreeEmbedmentOrProjectile(obj->prop);
				propActivate(obj->prop);
			}
#endif

			something = propPickupByPlayer(obj->prop, 0);
			propExecuteTickOperation(obj->prop, something);
			playernum = playermgrGetPlayerNumByProp(chr->prop);
			obj2->hidden = (playernum << 28) |
				(obj2->hidden & 0x0fffffff);
			setCurrentPlayerNum(prevplayernum);
		} else {
			if (obj->prop->parent) {
				objDetach(obj->prop);
			} else {
				propDeregisterRooms(obj->prop);
				propDelist(obj->prop);
				propDisable(obj->prop);
			}
			if (obj->type != OBJTYPE_WEAPON ||
					chrEquipWeapon((struct weaponobj *)obj, chr) == 0) {
				propReparent(obj->prop, chr->prop);
			}
		}
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_give_object_to_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action give_object_to_chr tag=%d chr=%d applied=%d source=%s objects=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv",
			tag_id, chrnum, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_give_object_to_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteObjectMoveToPad(s32 tag_id, s32 padnum)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireObjectInteractionNode("object_move_to_pad",
			s_ActiveScenarioGraphs.level_ai_object_move_to_pad_node_count, 1)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		Mtxf matrix;
		struct pad pad;
		RoomNum rooms[2];

		padUnpack(padnum, PADFIELD_POS | PADFIELD_LOOK |
			PADFIELD_UP | PADFIELD_ROOM, &pad);
		mtx00016d58(&matrix,
			0, 0, 0,
			-pad.look.x, -pad.look.y, -pad.look.z,
			pad.up.x, pad.up.y, pad.up.z);
		if (obj->model) {
			mtx00015f04(obj->model->scale, &matrix);
		}
		rooms[0] = pad.room;
		rooms[1] = -1;
		func0f06a730(obj, &pad.pos, &matrix, rooms, &pad.pos);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_object_move_to_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action object_move_to_pad tag=%d pad=%d applied=%d source=%s objects=%s pads=%s backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv+pads.tsv",
			tag_id, padnum, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_object_move_to_pad_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDoAnimation(struct chrdata *basechr,
	u16 anim_id, s32 startframe, s32 endframe, u8 chranimflags, s32 merge,
	s32 target_chr, s32 speed_divisor)
{
	struct chrdata *chr;
	f32 fstartframe;
	f32 fendframe;

	if (!s_aiGraphRequireAnimationNode("chr_do_animation",
			s_ActiveScenarioGraphs.level_ai_chr_do_animation_node_count)) {
		return 0;
	}

	chr = basechr ? chrFindById(basechr, target_chr) : NULL;

	if (startframe == 0xffff) {
		fstartframe = 0;
	} else if (startframe == 0xfffe) {
		fstartframe = animGetNumFrames(anim_id) - 1;
	} else {
		fstartframe = startframe;
	}

	if (endframe == 0xffff) {
		fendframe = -1.0f;
	} else {
		fendframe = endframe;
	}

	if (chr && chr->model) {
		f32 speed = 1.0f / (s32)speed_divisor;

		if (playerCurrentCutsceneInProgress()) {
			if (startframe != 0xfffe) {
#if PAL
				fstartframe += var8009e388pf * speed;
#else
				fstartframe += g_CutsceneFrameOverrun240 * speed * 0.25f;
#endif
			}

			chr->prop->propupdate240 = 0;
		}

		chrTryStartAnim(chr, anim_id, fstartframe, fendframe,
			chranimflags, merge, speed);

		if (startframe == 0xfffe) {
			chr0f0220ec(chr, 1, true);

			if (chr->prop->type == PROPTYPE_PLAYER) {
				u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
				struct player *player = g_Vars.players[playernum];
				player->vv_ground = chr->ground;
				player->vv_manground = chr->ground;
			}
		}
	}

	g_Vars.aioffset += 12;

	if (!s_ActiveScenarioGraphs.ai_action_chr_do_animation_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_do_animation chr=%d anim=%u target=%d source=%s backend=graph.ai.action.animation+ai/ailists.tsv",
			chr ? 1 : 0, (unsigned)anim_id, target_chr,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_do_animation_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteBeSurprisedOneHand(struct chrdata *chr)
{
	if (!s_aiGraphRequireAnimationNode("be_surprised_one_hand",
			s_ActiveScenarioGraphs.level_ai_be_surprised_one_hand_node_count)) {
		return 0;
	}

	chrTrySurprisedOneHand(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_be_surprised_one_hand_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action be_surprised_one_hand chr=%d source=%s backend=graph.ai.action.animation+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_be_surprised_one_hand_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteBeSurprisedLookAround(struct chrdata *chr)
{
	if (!s_aiGraphRequireAnimationNode("be_surprised_look_around",
			s_ActiveScenarioGraphs.level_ai_be_surprised_look_around_node_count)) {
		return 0;
	}

	chrTrySurprisedLookAround(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_be_surprised_look_around_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action be_surprised_look_around chr=%d source=%s backend=graph.ai.action.animation+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_be_surprised_look_around_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteBeSurprisedSurrender(struct chrdata *chr)
{
	if (!s_aiGraphRequireAnimationNode("be_surprised_surrender",
			s_ActiveScenarioGraphs.level_ai_be_surprised_surrender_node_count)) {
		return 0;
	}

	chrTrySurprisedSurrender(chr);
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_be_surprised_surrender_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action be_surprised_surrender chr=%d source=%s backend=graph.ai.action.animation+ai/ailists.tsv",
			chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_be_surprised_surrender_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRandom(struct chrdata *chr)
{
	if (!s_aiGraphRequireRandomControlNode("action", "random",
			s_ActiveScenarioGraphs.level_ai_random_node_count)) {
		return 0;
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("random",
			"missing chrdata for random state");
	}

	chr->random = rngRandom() & 0xff;
	g_Vars.aioffset += 2;

	if (!s_ActiveScenarioGraphs.ai_action_random_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action random value=%u source=%s backend=graph.ai.control.random+ai/ailists.tsv",
			(unsigned)chr->random, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_random_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfRandomLessThan(struct chrdata *chr,
	struct chopperobj *hovercar, s32 threshold, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireRandomControlNode("condition",
			"if_random_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_random_less_than_node_count)) {
		return 0;
	}

	branch_taken = (chr && chr->random < threshold) ||
		(hovercar && ((u8)rngRandom()) < threshold);
	s_aiGraphApplyBranch(branch_taken, label, 4);

	if (!s_ActiveScenarioGraphs.ai_condition_if_random_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_random_less_than threshold=%d label=%d branch=%d source=%s backend=graph.ai.control.random+ai/ailists.tsv",
			threshold, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_random_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfRandomGreaterThan(struct chrdata *chr,
	struct chopperobj *hovercar, s32 threshold, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireRandomControlNode("condition",
			"if_random_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_random_greater_than_node_count)) {
		return 0;
	}

	branch_taken = (chr && chr->random > threshold) ||
		(hovercar && ((u8)rngRandom()) > threshold);
	s_aiGraphApplyBranch(branch_taken, label, 4);

	if (!s_ActiveScenarioGraphs.ai_condition_if_random_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_random_greater_than threshold=%d label=%d branch=%d source=%s backend=graph.ai.control.random+ai/ailists.tsv",
			threshold, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_random_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePrint(void)
{
	u32 len;
	u32 result;

	if (!s_aiGraphRequireDebugNoOpNode("print",
			s_ActiveScenarioGraphs.level_ai_print_node_count)) {
		return 0;
	}

	result = dprint();
	if (result) {
		result = 2;
	}
	if (result == 2) {
		/* Preserve the original command's observable no-op branch. */
	}

	len = chraiGetCommandLength(g_Vars.ailist, g_Vars.aioffset);
	g_Vars.aioffset += len;

	if (!s_ActiveScenarioGraphs.ai_action_print_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action print len=%u source=%s backend=graph.ai.action.debug_noop+ai/ailists.tsv",
			(unsigned)len, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_print_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteNoOp(const char *opcode_name, s32 len)
{
	if (!s_aiGraphRequireDebugNoOpNode("noop",
			s_ActiveScenarioGraphs.level_ai_noop_node_count)) {
		return 0;
	}

	g_Vars.aioffset += len;

	if (!s_ActiveScenarioGraphs.ai_action_noop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action noop opcode=%s len=%d source=%s backend=graph.ai.action.debug_noop+ai/ailists.tsv",
			opcode_name && opcode_name[0] ? opcode_name : "?",
			len, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_noop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetList(s32 target_preset, u16 list_id)
{
	u8 *ailist;
	struct chrdata *chr;

	if (!s_aiGraphRequireListControlNode("set_list",
			s_ActiveScenarioGraphs.level_ai_set_list_node_count)) {
		return 0;
	}

	ailist = ailistFindById(list_id);
	if ((target_preset & 0xff) == CHR_SELF) {
		g_Vars.ailist = ailist;
		g_Vars.aioffset = 0;
	} else {
		chr = chrFindById(g_Vars.chrdata, target_preset);
		if (chr) {
			chr->ailist = ailist;
			chr->aioffset = 0;
			chr->sleep = 0;
		}
		g_Vars.aioffset += 5;
	}

	if (!s_ActiveScenarioGraphs.ai_action_set_list_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_list target=%d list=%u source=%s backend=graph.ai.action.list_control+ai/ailists.tsv",
			target_preset, (unsigned)list_id,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_list_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetReturnList(s32 target_preset, u16 list_id)
{
	struct chrdata *chr;

	if (!s_aiGraphRequireListControlNode("set_return_list",
			s_ActiveScenarioGraphs.level_ai_set_return_list_node_count)) {
		return 0;
	}

	if (g_Vars.chrdata) {
		if (target_preset == CHR_SELF) {
			g_Vars.chrdata->aireturnlist = list_id;
		} else {
			chr = chrFindById(g_Vars.chrdata, target_preset);
			if (chr) {
				chr->aireturnlist = list_id;
			}
		}
	} else if (g_Vars.truck) {
		g_Vars.truck->aireturnlist = list_id;
	} else if (g_Vars.heli) {
		g_Vars.heli->aireturnlist = list_id;
	} else if (g_Vars.hovercar) {
		g_Vars.hovercar->aireturnlist = list_id;
	}

	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_set_return_list_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_return_list target=%d list=%u source=%s backend=graph.ai.action.list_control+ai/ailists.tsv",
			target_preset, (unsigned)list_id,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_return_list_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetShotList(u16 list_id)
{
	if (!s_aiGraphRequireListControlNode("set_shot_list",
			s_ActiveScenarioGraphs.level_ai_set_shot_list_node_count)) {
		return 0;
	}
	if (g_Vars.chrdata) {
		g_Vars.chrdata->aishotlist = list_id;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_set_shot_list_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_shot_list list=%u applied=%d source=%s backend=graph.ai.action.list_control+ai/ailists.tsv",
			(unsigned)list_id, g_Vars.chrdata ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_shot_list_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteReturnList(void)
{
	u16 list_id = 0;
	u8 *ailist = NULL;

	if (!s_aiGraphRequireListControlNode("return_list",
			s_ActiveScenarioGraphs.level_ai_return_list_node_count)) {
		return 0;
	}
	if (g_Vars.chrdata) {
		list_id = g_Vars.chrdata->aireturnlist;
	} else if (g_Vars.truck) {
		list_id = (u16)g_Vars.truck->aireturnlist;
	} else if (g_Vars.heli) {
		list_id = (u16)g_Vars.heli->aireturnlist;
	} else if (g_Vars.hovercar) {
		list_id = (u16)g_Vars.hovercar->aireturnlist;
	}
	ailist = ailistFindById(list_id);
	g_Vars.ailist = ailist;
	g_Vars.aioffset = 0;
	if (!s_ActiveScenarioGraphs.ai_action_return_list_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action return_list list=%u source=%s backend=graph.ai.action.list_control+ai/ailists.tsv",
			(unsigned)list_id, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_return_list_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphExecuteSetChrListField(const char *action,
	s32 node_count, s16 *field, u16 list_id, s32 *logged)
{
	if (!s_aiGraphRequireListControlNode(action, node_count)) {
		return 0;
	}
	if (field) {
		*field = (s16)list_id;
	}
	g_Vars.aioffset += 4;
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s list=%u applied=%d source=%s backend=graph.ai.action.list_control+ai/ailists.tsv",
			action, (unsigned)list_id, field ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPunchDodgeList(u16 list_id)
{
	return s_aiGraphExecuteSetChrListField("set_punch_dodge_list",
		s_ActiveScenarioGraphs.level_ai_set_punch_dodge_list_node_count,
		g_Vars.chrdata ? &g_Vars.chrdata->aipunchdodgelist : NULL,
		list_id, &s_ActiveScenarioGraphs.ai_action_set_punch_dodge_list_logged);
}

s32 scenarioSourceAiGraphExecuteSetShootingAtMeList(u16 list_id)
{
	return s_aiGraphExecuteSetChrListField("set_shooting_at_me_list",
		s_ActiveScenarioGraphs.level_ai_set_shooting_at_me_list_node_count,
		g_Vars.chrdata ? &g_Vars.chrdata->aishootingatmelist : NULL,
		list_id,
		&s_ActiveScenarioGraphs.ai_action_set_shooting_at_me_list_logged);
}

s32 scenarioSourceAiGraphExecuteSetDarkRoomList(u16 list_id)
{
	return s_aiGraphExecuteSetChrListField("set_dark_room_list",
		s_ActiveScenarioGraphs.level_ai_set_dark_room_list_node_count,
		g_Vars.chrdata ? &g_Vars.chrdata->aidarkroomlist : NULL,
		list_id, &s_ActiveScenarioGraphs.ai_action_set_dark_room_list_logged);
}

s32 scenarioSourceAiGraphExecuteSetPlayerDeadList(u16 list_id)
{
	return s_aiGraphExecuteSetChrListField("set_player_dead_list",
		s_ActiveScenarioGraphs.level_ai_set_player_dead_list_node_count,
		g_Vars.chrdata ? &g_Vars.chrdata->aiplayerdeadlist : NULL,
		list_id, &s_ActiveScenarioGraphs.ai_action_set_player_dead_list_logged);
}

s32 scenarioSourceAiGraphExecuteTryStartAlarm(struct chrdata *chr,
	s32 pad_id, s32 label)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_try_start_alarm_node_count != 1) {
		return s_aiGraphRuntimeFailure("try_start_alarm",
			"missing scenario.ai.action.try_start_alarm node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("try_start_alarm", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("try_start_alarm",
			"missing ai/ailists.tsv source");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("try_start_alarm",
			"missing pads.tsv source");
	}

	if (chrTryStartAlarm(chr, pad_id)) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, (u8)label);
	} else {
		g_Vars.aioffset += 5;
	}

	if (!s_ActiveScenarioGraphs.ai_action_try_start_alarm_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_start_alarm pad=%d label=%d source=%s backend=graph.ai.action.try_start_alarm+pads.tsv",
			pad_id, label, s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_try_start_alarm_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteActivateAlarm(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_activate_alarm_node_count != 1) {
		return s_aiGraphRuntimeFailure("activate_alarm",
			"missing scenario.ai.action.activate_alarm node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("activate_alarm",
			"missing ai/ailists.tsv source");
	}
	alarmActivate();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_activate_alarm_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action activate_alarm source=%s backend=graph.ai.action.activate_alarm+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_activate_alarm_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDeactivateAlarm(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_deactivate_alarm_node_count != 1) {
		return s_aiGraphRuntimeFailure("deactivate_alarm",
			"missing scenario.ai.action.deactivate_alarm node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("deactivate_alarm",
			"missing ai/ailists.tsv source");
	}
	alarmDeactivate();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_deactivate_alarm_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action deactivate_alarm source=%s backend=graph.ai.action.deactivate_alarm+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_deactivate_alarm_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphExecuteGoToPad(struct chrdata *chr, s32 pad,
	u32 goposflags, const char *action, const char *backend, s32 *logged)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure(action, "missing chr");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure(action, "missing pads.tsv source");
	}
	chrGoToPad(chr, pad, goposflags);
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s pad=%d source=%s %s",
			action, pad, s_ActiveScenarioGraphs.pads_path, backend);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteJogToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_jog_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("jog_to_pad",
			"missing scenario.ai.action.jog_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_JOG,
		"jog_to_pad", "backend=graph.ai.action.jog_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_jog_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteGoToPadPreset(struct chrdata *chr, s32 speed_code)
{
	u32 goposflags;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_goto_pad_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("go_to_pad_preset",
			"missing scenario.ai.action.go_to_pad_preset node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("go_to_pad_preset", "missing chr");
	}

	switch (speed_code) {
	case 0:
		goposflags = GOPOSFLAG_WALK;
		break;
	case 1:
		goposflags = GOPOSFLAG_JOG;
		break;
	default:
		goposflags = GOPOSFLAG_RUN;
		break;
	}

	return s_aiGraphExecuteGoToPad(chr, chr->padpreset1, goposflags,
		"go_to_pad_preset",
		"backend=graph.ai.action.go_to_pad_preset+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_goto_pad_preset_logged);
}

s32 scenarioSourceAiGraphExecuteWalkToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_walk_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("walk_to_pad",
			"missing scenario.ai.action.walk_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_WALK,
		"walk_to_pad", "backend=graph.ai.action.walk_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_walk_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteRunToPad(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_run_to_pad_node_count != 1) {
		return s_aiGraphRuntimeFailure("run_to_pad",
			"missing scenario.ai.action.run_to_pad node");
	}
	return s_aiGraphExecuteGoToPad(chr, pad, GOPOSFLAG_RUN,
		"run_to_pad", "backend=graph.ai.action.run_to_pad+pads.tsv",
		&s_ActiveScenarioGraphs.ai_action_run_to_pad_logged);
}

s32 scenarioSourceAiGraphExecuteSetPath(struct chrdata *chr, s32 path_id)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_path_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_path",
			"missing scenario.ai.action.set_path node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_path", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.paths_path[0]) {
		return s_aiGraphRuntimeFailure("set_path",
			"missing navigation/paths.tsv source");
	}
	chrSetPath(chr, (u32)path_id);
	if (!s_ActiveScenarioGraphs.ai_action_set_path_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_path path=%d source=%s backend=graph.ai.action.set_path+navigation/paths.tsv",
			path_id, s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs.ai_action_set_path_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStartPatrol(struct chrdata *chr)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_start_patrol_node_count != 1) {
		return s_aiGraphRuntimeFailure("start_patrol",
			"missing scenario.ai.action.start_patrol node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("start_patrol", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.paths_path[0]) {
		return s_aiGraphRuntimeFailure("start_patrol",
			"missing navigation/paths.tsv source");
	}
	chrTryStartPatrol(chr);
	if (!s_ActiveScenarioGraphs.ai_action_start_patrol_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action start_patrol path=%u source=%s backend=graph.ai.action.start_patrol+navigation/paths.tsv",
			(unsigned)chr->path, s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs.ai_action_start_patrol_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPadPreset(struct chrdata *chr, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_pad_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_pad_preset",
			"missing scenario.ai.action.set_pad_preset node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_pad_preset", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("set_pad_preset",
			"missing pads.tsv source");
	}
	chrSetPadPreset(chr, pad);
	if (!s_ActiveScenarioGraphs.ai_action_set_pad_preset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_pad_preset pad=%d source=%s backend=graph.ai.action.set_pad_preset+pads.tsv",
			pad, s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_set_pad_preset_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetPadPreset(struct chrdata *basechr,
	s32 chrnum, s32 pad)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_pad_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_pad_preset",
			"missing scenario.ai.action.chr_set_pad_preset node");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_pad_preset",
			"missing pads.tsv source");
	}
	chrSetPadPresetByChrnum(basechr, chrnum, pad);
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_pad_preset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_pad_preset chr=%d pad=%d source=%s backend=graph.ai.action.chr_set_pad_preset+pads.tsv",
			chrnum, pad, s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_pad_preset_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrCopyPadPreset(struct chrdata *basechr,
	s32 src_chrnum, s32 dst_chrnum)
{
	struct chrdata *chrsrc;
	struct chrdata *chrdst;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_copy_pad_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_copy_pad_preset",
			"missing scenario.ai.action.chr_copy_pad_preset node");
	}
	chrsrc = chrFindById(basechr, src_chrnum);
	chrdst = chrFindById(basechr, dst_chrnum);
	if (!chrsrc || !chrdst) {
		return s_aiGraphRuntimeFailure("chr_copy_pad_preset",
			"missing source or destination chr");
	}
	chrdst->padpreset1 = chrsrc->padpreset1;
	if (!s_ActiveScenarioGraphs.ai_action_chr_copy_pad_preset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_copy_pad_preset src_chr=%d dst_chr=%d pad=%d source=%s backend=graph.ai.action.chr_copy_pad_preset+chrstate",
			src_chrnum, dst_chrnum, chrdst->padpreset1,
			s_ActiveScenarioGraphs.ai_lists_path[0]
				? s_ActiveScenarioGraphs.ai_lists_path : "(missing)");
		s_ActiveScenarioGraphs.ai_action_chr_copy_pad_preset_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrPreset(struct chrdata *chr,
	s32 chrpreset)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chr_preset_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chr_preset",
			"missing scenario.ai.action.set_chr_preset node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_chr_preset", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chr_preset",
			"missing ai/ailists.tsv source");
	}
	chrSetChrPreset(chr, chrpreset);
	if (!s_ActiveScenarioGraphs.ai_action_set_chr_preset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_preset chrpreset=%d source=%s backend=graph.ai.action.set_chr_preset+ai/ailists.tsv",
			chrpreset, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_preset_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrTarget(struct chrdata *basechr,
	s32 chrnum, s32 chrpreset)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chr_target_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chr_target",
			"missing scenario.ai.action.set_chr_target node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chr_target",
			"missing ai/ailists.tsv source");
	}
	chrSetChrPresetByChrnum(basechr, chrnum, chrpreset);
	if (!s_ActiveScenarioGraphs.ai_action_set_chr_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_target chr=%d chrpreset=%d source=%s backend=graph.ai.action.set_chr_target+ai/ailists.tsv",
			chrnum, chrpreset, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetAction(struct chrdata *chr,
	s32 action, s32 clear_orders)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_action_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_action",
			"missing scenario.ai.action.set_action node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_action", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_action",
			"missing ai/ailists.tsv source");
	}
	chr->myaction = (u8)action;
	if (clear_orders) {
		chr->orders = 0;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_action_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_action action=%d clear_orders=%d source=%s backend=graph.ai.action.set_action+ai/ailists.tsv",
			action, clear_orders ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_action_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetTeamOrders(struct chrdata *chr,
	s32 *out_follow_label)
{
	struct chrnumaction *chraction;
	struct chrnumaction chractions[50];
	s32 chrcount;
	s32 num;
	s16 *chrnums;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_team_orders_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_team_orders",
			"missing scenario.ai.action.set_team_orders node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_team_orders", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_team_orders",
			"missing ai/ailists.tsv source");
	}
	if (out_follow_label) {
		*out_follow_label = 0;
	}

	chrcount = 1;
	chrnums = squadronGetChrIds(chr->squadron);

	chraction = chractions;
	chraction->chrnum = chr->chrnum;
	chraction->myaction = chr->myaction;
	chraction++;

	if (chrnums) {
		while (*chrnums != -2) {
			struct chrdata *target = chrFindByLiteralId(*chrnums);

			if (target && target->model
					&& !chrIsDead(target)
					&& target->actiontype != ACT_DEAD
					&& chrCompareTeams(chr, target, COMPARE_FRIENDS)
					&& chr->chrnum != target->chrnum) {
				if (target->myaction == MA_COVERWAIT
						|| target->myaction == MA_NORMAL
						|| target->myaction == MA_WAITING
						|| target->myaction == MA_SHOOTING) {
					if (chrGetDistanceToChr(chr, target->chrnum) < 3500
							&& chrcount < (s32)ARRAYCOUNT(chractions)) {
						chrcount++;
						chraction->chrnum = target->chrnum;
						chraction->myaction = target->myaction;
						chraction++;
					}
				}
			}

			chrnums++;
		}
	}

	chraction->myaction = MA_END;

	if (chrcount != 1) {
		chraction = &chractions[1];
		num = 1;

		while (chraction->myaction != MA_END) {
			struct chrdata *target = chrFindByLiteralId(chraction->chrnum);

			if (!target) {
				chraction++;
				continue;
			}

			switch (chractions[0].myaction) {
			case MA_COVERGOTO:
				if (!chrIsInTargetsFovX(target, 45)) {
					target->orders = MA_SHOOTING;
				}
				break;
			case MA_COVERBREAK:
				if (!chrIsInTargetsFovX(target, 30)) {
					target->orders = MA_SHOOTING;
				}
				num++;
				break;
			case MA_COVERSEEN:
				if (!chrIsInTargetsFovX(target, 30)) {
					target->orders = MA_SHOOTING;
					chr->orders = MA_COVERGOTO;
				}
				num++;
				break;
			case MA_FLANKLEFT:
				if (chrIsInTargetsFovX(target, 50)) {
					target->orders = MA_FLANKRIGHT;
				} else {
					target->orders = MA_SHOOTING;
				}
				num++;
				chr->orders = MA_FLANKLEFT;
				break;
			case MA_FLANKRIGHT:
				if (chrIsInTargetsFovX(target, 50)) {
					target->orders = MA_FLANKLEFT;
				} else {
					target->orders = MA_SHOOTING;
				}
				num++;
				chr->orders = MA_FLANKRIGHT;
				break;
			case MA_DODGE:
				if (!chrIsInTargetsFovX(target, 30) &&
						chrHasFlagById(target, CHR_SELF,
							CHRFLAG0_CAN_BACKOFF, BANK_0)) {
					target->orders = MA_WITHDRAW;
				} else {
					target->orders = MA_SHOOTING;
				}
				num++;
				break;
			case MA_GRENADE:
				if (num < 2) {
					target->orders = MA_WAITING;
				} else if (chrHasFlagById(target, CHR_SELF,
						CHRFLAG0_CAN_BACKOFF, BANK_0)) {
					target->orders = MA_WITHDRAW;
				}
				num++;
				break;
			case MA_WAITSEEN:
				if (chrIsInTargetsFovX(target, 30) &&
						chrHasFlagById(target, CHR_SELF,
							CHRFLAG0_CAN_BACKOFF, BANK_0)) {
					target->orders = MA_WITHDRAW;
				} else {
					target->orders = MA_SHOOTING;
				}
				num++;
				break;
			case MA_WITHDRAW:
				if (chrHasFlagById(target, CHR_SELF,
						CHRFLAG0_CAN_BACKOFF, BANK_0)) {
					target->orders = MA_WITHDRAW;
				}
				break;
			}

			chraction++;
		}

		if (out_follow_label && num != 1) {
			*out_follow_label = 1;
		}
	} else {
		num = 1;
	}

	if (!s_ActiveScenarioGraphs.ai_action_set_team_orders_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_team_orders action=%d chrs=%d follow_label=%d source=%s backend=graph.ai.action.set_team_orders+ai/ailists.tsv",
			chr->myaction, chrcount, out_follow_label ? *out_follow_label : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_team_orders_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRetreat(struct chrdata *chr,
	s32 speed, s32 operation)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_retreat_node_count != 1) {
		return s_aiGraphRuntimeFailure("retreat",
			"missing scenario.ai.action.retreat node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("retreat", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("retreat",
			"missing ai/ailists.tsv source");
	}

	if (operation == 0) {
		chrRunFromPos(chr, speed, (speed & 0x10) ? 400.0f : 10000.0f,
			&chr->runfrompos);
	} else if (operation == 1) {
		struct prop *target = chrGetTargetProp(chr);
		if (!target) {
			return s_aiGraphRuntimeFailure("retreat",
				"missing target prop");
		}
		chrRunFromPos(chr, speed, 10000, &target->pos);
	} else {
		chrAssignCoverByCriteria(chr,
			COVERCRITERIA_FURTHEREST |
			COVERCRITERIA_DISTTOTARGET |
			COVERCRITERIA_ONLYNEIGHBOURINGROOMS |
			COVERCRITERIA_ROOMSFROMME, 0);
		chrGoToCover(chr, speed);
	}

	if (!s_ActiveScenarioGraphs.ai_action_retreat_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action retreat speed=%d operation=%d source=%s backend=graph.ai.action.retreat+ai/ailists.tsv",
			speed, operation, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_retreat_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphExecuteFindCoverByCriteria(struct chrdata *chr,
	u16 criteria, s32 refdist, const char *action, s32 node_count,
	const char *backend, s32 *logged, s32 *out_assigned)
{
	s32 assigned;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action,
			"missing scenario.ai.action cover-search node");
	}
	if (!chr || !chr->prop) {
		return s_aiGraphRuntimeFailure(action, "missing chr/prop");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing navigation/covers.tsv source");
	}

	assigned = chrAssignCoverByCriteria(chr, criteria, refdist);
	if (out_assigned) {
		*out_assigned = assigned != -1;
	}
	if (logged && !*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s criteria=0x%04x refdist=%d assigned=%d source=%s %s",
			action, (unsigned)criteria, refdist, assigned,
			s_ActiveScenarioGraphs.covers_path, backend);
		*logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteFindCover(struct chrdata *chr,
	u16 criteria, s32 *out_assigned)
{
	return s_aiGraphExecuteFindCoverByCriteria(chr, criteria, 0,
		"find_cover",
		s_ActiveScenarioGraphs.level_ai_find_cover_node_count,
		"backend=graph.ai.action.find_cover+navigation/covers.tsv",
		&s_ActiveScenarioGraphs.ai_action_find_cover_logged,
		out_assigned);
}

s32 scenarioSourceAiGraphExecuteFindCoverWithinDist(struct chrdata *chr,
	u16 criteria, s32 refdist, s32 *out_assigned)
{
	return s_aiGraphExecuteFindCoverByCriteria(chr, criteria, refdist,
		"find_cover_within_dist",
		s_ActiveScenarioGraphs.level_ai_find_cover_within_dist_node_count,
		"backend=graph.ai.action.find_cover_within_dist+navigation/covers.tsv",
		&s_ActiveScenarioGraphs.ai_action_find_cover_within_dist_logged,
		out_assigned);
}

s32 scenarioSourceAiGraphExecuteFindCoverOutsideDist(struct chrdata *chr,
	u16 criteria, s32 refdist, s32 *out_assigned)
{
	return s_aiGraphExecuteFindCoverByCriteria(chr, criteria, -refdist,
		"find_cover_outside_dist",
		s_ActiveScenarioGraphs.level_ai_find_cover_outside_dist_node_count,
		"backend=graph.ai.action.find_cover_outside_dist+navigation/covers.tsv",
		&s_ActiveScenarioGraphs.ai_action_find_cover_outside_dist_logged,
		out_assigned);
}

s32 scenarioSourceAiGraphExecuteGoToCover(struct chrdata *chr, s32 speed)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_go_to_cover_node_count != 1) {
		return s_aiGraphRuntimeFailure("go_to_cover",
			"missing scenario.ai.action.go_to_cover node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("go_to_cover", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure("go_to_cover",
			"missing navigation/covers.tsv source");
	}

	chrGoToCover(chr, speed);
	if (!s_ActiveScenarioGraphs.ai_action_go_to_cover_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action go_to_cover speed=%d source=%s backend=graph.ai.action.go_to_cover+navigation/covers.tsv",
			speed, s_ActiveScenarioGraphs.covers_path);
		s_ActiveScenarioGraphs.ai_action_go_to_cover_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteCheckCoverOutOfSight(struct chrdata *chr,
	s32 *out_out_of_sight)
{
	s32 out_of_sight;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_check_cover_out_of_sight_node_count != 1) {
		return s_aiGraphRuntimeFailure("check_cover_out_of_sight",
			"missing scenario.ai.action.check_cover_out_of_sight node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("check_cover_out_of_sight",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure("check_cover_out_of_sight",
			"missing navigation/covers.tsv source");
	}

	out_of_sight = chrCheckCoverOutOfSight(chr, chr->cover, false) ? 1 : 0;
	if (out_out_of_sight) {
		*out_out_of_sight = out_of_sight;
	}
	if (!s_ActiveScenarioGraphs.ai_action_check_cover_out_of_sight_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action check_cover_out_of_sight cover=%d out_of_sight=%d source=%s backend=graph.ai.action.check_cover_out_of_sight+navigation/covers.tsv",
			chr->cover, out_of_sight, s_ActiveScenarioGraphs.covers_path);
		s_ActiveScenarioGraphs.ai_action_check_cover_out_of_sight_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteOrbitTarget(struct chrdata *chr,
	u32 angle, s32 try_alternate, s32 speed)
{
	struct prop *target;
	struct coord pos;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_orbit_target_node_count != 1) {
		return s_aiGraphRuntimeFailure("orbit_target",
			"missing scenario.ai.action.orbit_target node");
	}
	if (!chr || !chr->prop) {
		return s_aiGraphRuntimeFailure("orbit_target", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("orbit_target",
			"missing ai/ailists.tsv source");
	}
	target = chrGetTargetProp(chr);
	if (!target) {
		return s_aiGraphRuntimeFailure("orbit_target",
			"missing target prop");
	}

	chr0f04c874(chr, angle, &pos, (u8)try_alternate, (u8)speed);
	if (!s_ActiveScenarioGraphs.ai_action_orbit_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action orbit_target angle=%u alternate=%d speed=%d source=%s backend=graph.ai.action.orbit_target+ai/ailists.tsv",
			(unsigned)angle, try_alternate ? 1 : 0, speed,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_orbit_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate(
	struct chrdata *chr, s32 distance_units, s32 require_far,
	s32 *out_follow_label)
{
	f32 closest_distance;
	s16 candidate_chrnum;
	s16 *chrnums;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chr_preset_to_unalerted_teammate_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chr_preset_to_unalerted_teammate",
			"missing scenario.ai.action.set_chr_preset_to_unalerted_teammate node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_chr_preset_to_unalerted_teammate",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chr_preset_to_unalerted_teammate",
			"missing ai/ailists.tsv source");
	}
	if (out_follow_label) {
		*out_follow_label = 0;
	}

	closest_distance = 30999.9f;
	candidate_chrnum = -1;
	chrnums = teamGetChrIds(chr->team);

	if (chr->talktimer > TICKS(480) && chr->listening) {
		chr->listening = 0;
	}

	if (chrnums && require_far == 0) {
		for (; *chrnums != -2; chrnums++) {
			struct chrdata *target = chrFindByLiteralId(*chrnums);

			if (!target || !target->model ||
					chrIsDead(target) ||
					target->actiontype == ACT_DEAD ||
					target->actiontype == ACT_DIE ||
					target->actiontype == ACT_DRUGGEDKO ||
					target->actiontype == ACT_DRUGGEDDROP ||
					target->actiontype == ACT_DRUGGEDCOMINGUP ||
					target->alertness >= 100 ||
					(chr->squadron != target->squadron &&
						chr->squadron != 0xff) ||
					chr->chrnum == target->chrnum) {
				continue;
			}

			{
				f32 distance = chrGetDistanceToChr(chr,
					target->chrnum);

				if (distance < closest_distance &&
						(distance < 100.0f * distance_units ||
							distance_units == 0)) {
					closest_distance = distance;
					candidate_chrnum = target->chrnum;
				}
			}
		}
	}

	if (candidate_chrnum != -1) {
		chrSetChrPreset(chr, candidate_chrnum);
		if (out_follow_label) {
			*out_follow_label = 1;
		}
	}

	if (!s_ActiveScenarioGraphs.ai_action_set_chr_preset_to_unalerted_teammate_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_preset_to_unalerted_teammate candidate=%d distance_units=%d require_far=%d source=%s backend=graph.ai.action.set_chr_preset_to_unalerted_teammate+ai/ailists.tsv",
			candidate_chrnum, distance_units, require_far ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_preset_to_unalerted_teammate_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetSquadron(struct chrdata *chr,
	s32 squadron)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_squadron_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_squadron",
			"missing scenario.ai.action.set_squadron node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_squadron", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_squadron",
			"missing ai/ailists.tsv source");
	}
	chr->squadron = (u8)squadron;
	if (!s_ActiveScenarioGraphs.ai_action_set_squadron_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_squadron squadron=%d source=%s backend=graph.ai.action.set_squadron+ai/ailists.tsv",
			squadron, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_squadron_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteFaceCover(struct chrdata *chr,
	s32 *out_faced)
{
	s32 faced;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_face_cover_node_count != 1) {
		return s_aiGraphRuntimeFailure("face_cover",
			"missing scenario.ai.action.face_cover node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("face_cover", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure("face_cover",
			"missing navigation/covers.tsv source");
	}
	faced = chrFaceCover(chr) ? 1 : 0;
	if (out_faced) {
		*out_faced = faced;
	}
	if (!s_ActiveScenarioGraphs.ai_action_face_cover_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action face_cover faced=%d source=%s backend=graph.ai.action.face_cover+navigation/covers.tsv",
			faced, s_ActiveScenarioGraphs.covers_path);
		s_ActiveScenarioGraphs.ai_action_face_cover_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDangerCover(struct chrdata *chr)
{
	s32 assigned;
	s32 moved;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_danger_cover_node_count != 1) {
		return s_aiGraphRuntimeFailure("danger_cover",
			"missing scenario.ai.action.danger_cover node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("danger_cover", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure("danger_cover",
			"missing navigation/covers.tsv source");
	}

	assigned = -1;
	moved = 0;
	if (func0f03aca0(chr, 400, true) == 0) {
		assigned = chrAssignCoverAwayFromDanger(chr, 1000, 12000);
		if (assigned != -1) {
			chrGoToCover(chr, GOPOSFLAG_RUN);
			moved = 1;
		}
	}

	if (!s_ActiveScenarioGraphs.ai_action_danger_cover_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action danger_cover assigned=%d moved=%d source=%s backend=graph.ai.action.danger_cover+navigation/covers.tsv",
			assigned, moved, s_ActiveScenarioGraphs.covers_path);
		s_ActiveScenarioGraphs.ai_action_danger_cover_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteReleaseCover(struct chrdata *chr)
{
	s32 released = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_release_cover_node_count != 1) {
		return s_aiGraphRuntimeFailure("release_cover",
			"missing scenario.ai.action.release_cover node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("release_cover", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.covers_path[0]) {
		return s_aiGraphRuntimeFailure("release_cover",
			"missing navigation/covers.tsv source");
	}

	if (chr->cover >= 0) {
		coverSetInUse(chr->cover, 0);
		released = 1;
	}
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_release_cover_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action release_cover cover=%d released=%d source=%s backend=graph.ai.action.release_cover+navigation/covers.tsv",
			chr->cover, released, s_ActiveScenarioGraphs.covers_path);
		s_ActiveScenarioGraphs.ai_action_release_cover_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRebuildTeams(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_rebuild_teams_node_count != 1) {
		return s_aiGraphRuntimeFailure("rebuild_teams",
			"missing scenario.ai.action.rebuild_teams node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("rebuild_teams",
			"missing ai/ailists.tsv source");
	}
	rebuildTeams();
	if (!s_ActiveScenarioGraphs.ai_action_rebuild_teams_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action rebuild_teams source=%s backend=graph.ai.action.rebuild_teams+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_rebuild_teams_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRebuildSquadrons(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_rebuild_squadrons_node_count != 1) {
		return s_aiGraphRuntimeFailure("rebuild_squadrons",
			"missing scenario.ai.action.rebuild_squadrons node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("rebuild_squadrons",
			"missing ai/ailists.tsv source");
	}
	rebuildSquadrons();
	if (!s_ActiveScenarioGraphs.ai_action_rebuild_squadrons_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action rebuild_squadrons source=%s backend=graph.ai.action.rebuild_squadrons+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_rebuild_squadrons_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireCharacterConditionNode(const char *condition,
	s32 node_count);

s32 scenarioSourceAiGraphExecuteChrSetListening(struct chrdata *basechr,
	s32 chrnum, s32 listening)
{
	struct chrdata *chr;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_listening_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_listening",
			"missing scenario.ai.action.chr_set_listening node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_set_listening",
			"missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_listening",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	applied = 0;
	if (chr && chr->listening == 0) {
		chr->listening = (u8)listening;
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_listening_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_listening chr=%d value=%d applied=%d source=%s backend=graph.ai.action.chr_set_listening+ai/ailists.tsv",
			chrnum, listening, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_listening_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrNotTalking(s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_not_talking",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_not_talking_node_count)) {
		return 0;
	}
	chr = chrFindByLiteralId(chrnum);
	branch_taken = chr && chr->propsoundcount == 0;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_not_talking_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_not_talking chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_not_talking_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfOrders(struct chrdata *chr, s32 order,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_orders",
			s_ActiveScenarioGraphs.level_ai_if_orders_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->orders == order;
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs.ai_condition_if_orders_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_orders order=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			order, chr ? chr->orders : -1, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_orders_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHasOrders(struct chrdata *chr, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_has_orders",
			s_ActiveScenarioGraphs.level_ai_if_has_orders_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->orders;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_has_orders_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_has_orders current=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chr ? chr->orders : 0, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_has_orders_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction(
	struct chrdata *chr, s32 action, s32 label)
{
	s16 *chrnums;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_chr_in_squadron_doing_action",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_in_squadron_doing_action_node_count)) {
		return 0;
	}
	branch_taken = 0;
	if (chr) {
		chrnums = squadronGetChrIds(chr->squadron);
		if (chrnums) {
			for (; *chrnums != -2; chrnums++) {
				struct chrdata *other = chrFindByLiteralId(*chrnums);
				if (other && other->model && !chrIsDead(other) &&
						other->actiontype != ACT_DEAD &&
						chrCompareTeams(chr, other, COMPARE_FRIENDS) &&
						chr->chrnum != other->chrnum &&
						chrGetDistanceToChr(chr, other->chrnum) < 3500 &&
						other->myaction == action) {
					branch_taken = 1;
					break;
				}
			}
		}
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_in_squadron_doing_action_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_in_squadron_doing_action action=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			action, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_in_squadron_doing_action_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrListening(struct chrdata *basechr,
	s32 chrnum, s32 listening, s32 check_convtalk, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_listening",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_listening_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (!check_convtalk) {
		branch_taken = chr && chr->listening == listening;
	} else {
		branch_taken = basechr && basechr->convtalk == 0;
	}
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_listening_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_listening chr=%d listening=%d check_convtalk=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, listening, check_convtalk, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_listening_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNotListening(struct chrdata *chr,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_not_listening",
			s_ActiveScenarioGraphs
				.level_ai_if_not_listening_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->listening == 0;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_not_listening_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_not_listening label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_not_listening_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrInjuredTarget(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_injured_target",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_injured_target_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = chr && (chr->chrflags & CHRCFLAG_INJUREDTARGET);
	if (branch_taken) {
		chr->chrflags &= ~CHRCFLAG_INJUREDTARGET;
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_injured_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_injured_target chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_injured_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfAction(struct chrdata *chr, s32 action,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_action",
			s_ActiveScenarioGraphs.level_ai_if_action_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->myaction == action;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_action_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_action action=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			action, chr ? chr->myaction : -1, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_action_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan(
	struct chrdata *basechr, s32 chrnum, s32 ammo_type, s32 quantity,
	s32 label)
{
	struct chrdata *chr;
	s32 ammo_count;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_chr_ammo_quantity_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_ammo_quantity_less_than_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	ammo_count = 0;
	branch_taken = 0;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		ammo_count = bgunGetAmmoCount(ammo_type);
		branch_taken = ammo_count < quantity;
		setCurrentPlayerNum(prevplayernum);
	}
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_ammo_quantity_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_ammo_quantity_less_than chr=%d ammo_type=%d quantity=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, ammo_type, quantity, ammo_count, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_ammo_quantity_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrTarget(struct chrdata *basechr,
	s32 chrnum, s32 target_chrnum, s32 require_any_target, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_target",
			s_ActiveScenarioGraphs.level_ai_if_chr_target_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = 0;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		/* Original command intentionally ignores player chrs. */
	} else if (chrnum != CHR_BOND) {
		if (require_any_target == 0) {
			struct chrdata *target = chrFindById(basechr, target_chrnum);
			if (chr && target && target->prop &&
					chrGetTargetProp(chr) == target->prop) {
				branch_taken = 1;
			}
		} else if (chr && chr->target != -1 && chr->prop) {
			branch_taken = 1;
		}
	}
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_target chr=%d target_chr=%d any=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, target_chrnum, require_any_target, label,
			branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam(
	struct chrdata *chr, s32 comparison, s32 label)
{
	struct chrdata *preset_chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_compare_chr_presets_team",
			s_ActiveScenarioGraphs
				.level_ai_if_compare_chr_presets_team_node_count)) {
		return 0;
	}
	preset_chr = chrFindById(chr, CHR_PRESET);
	if (!preset_chr || (!preset_chr->model &&
			preset_chr->prop->type != PROPTYPE_PLAYER)) {
		chrSetChrPreset(chr, CHR_BOND);
		preset_chr = chrFindById(chr, CHR_PRESET);
	}
	branch_taken = preset_chr && chrCompareTeams(preset_chr, chr, comparison);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_compare_chr_presets_team_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_compare_chr_presets_team comparison=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			comparison, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_compare_chr_presets_team_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHuman(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_human",
			s_ActiveScenarioGraphs.level_ai_if_human_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = chr && chr->prop && CHRRACE(chr) == RACE_HUMAN;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_human_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_human chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_human_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSkedar(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_skedar",
			s_ActiveScenarioGraphs.level_ai_if_skedar_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = chr && chr->prop && CHRRACE(chr) == RACE_SKEDAR;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_skedar_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_skedar chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.intent_status+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_skedar_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequirePropPresetTargetNode(const char *kind,
	const char *name, s32 node_count, s32 require_objects, s32 require_pads)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai.%s.%s node",
			kind, name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			(require_objects && !s_ActiveScenarioGraphs.objects_path[0]) ||
			(require_pads && !s_ActiveScenarioGraphs.pads_path[0])) {
		s_aiGraphRuntimeFailure(name,
			require_pads
				? "missing ai/ailists.tsv, objects.tsv, or pads.tsv source"
				: require_objects
					? "missing ai/ailists.tsv or objects.tsv source"
					: "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget(
	struct chrdata *chr, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequirePropPresetTargetNode("condition",
		"if_prop_preset_blocking_sight_to_target",
		s_ActiveScenarioGraphs
			.level_ai_if_prop_preset_blocking_sight_to_target_node_count,
		1, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chrIsPropPresetBlockingSightToTarget(chr);
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_prop_preset_blocking_sight_to_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_prop_preset_blocking_sight_to_target label=%d branch=%d source=%s objects=%s backend=graph.ai.condition.prop_target+ai/ailists.tsv+objects.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_prop_preset_blocking_sight_to_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset(struct chrdata *chr)
{
	s32 required;
	s32 cleared = 0;

	required = s_aiGraphRequirePropPresetTargetNode("action",
		"remove_object_at_prop_preset",
		s_ActiveScenarioGraphs.level_ai_remove_object_at_prop_preset_node_count,
		1, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr && chr->proppreset1 >= 0) {
		struct prop *prop = &g_Vars.props[chr->proppreset1];
		if (prop->obj) {
			prop->obj->hidden &= ~OBJHFLAG_OCCUPIEDCHAIR;
			cleared = 1;
		}
		chr->proppreset1 = -1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_remove_object_at_prop_preset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action remove_object_at_prop_preset cleared=%d source=%s objects=%s backend=graph.ai.action.prop_target+ai/ailists.tsv+objects.tsv",
			cleared, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_remove_object_at_prop_preset_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan(
	struct chrdata *chr, f32 value, s32 label)
{
	s32 required;
	s32 branch_taken = 0;
	f32 height = 0.0f;

	required = s_aiGraphRequirePropPresetTargetNode("condition",
		"if_prop_preset_height_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_prop_preset_height_less_than_node_count,
		1, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr && chr->proppreset1 >= 0) {
		struct prop *prop = &g_Vars.props[chr->proppreset1];
		f32 ymax;
		f32 ymin;
		f32 radius;
		propGetBbox(prop, &radius, &ymax, &ymin);
		height = ymax - ymin;
		branch_taken = height < value;
	}
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_prop_preset_height_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_prop_preset_height_less_than value=%.3f height=%.3f label=%d branch=%d source=%s objects=%s backend=graph.ai.condition.prop_target+ai/ailists.tsv+objects.tsv",
			(double)value, (double)height, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_prop_preset_height_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 chrnum, s32 mode, s32 flags)
{
	s32 required;
	s16 newtarget = -1;
	s32 applied = 0;

	required = s_aiGraphRequirePropPresetTargetNode("action", "set_target",
		s_ActiveScenarioGraphs.level_ai_set_target_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr) {
		if (flags != 0) {
			s_aiGraphRuntimeFailure("set_target",
				"unsupported legacy set_target flags");
			return 1;
		}
		if (mode == 0) {
			newtarget = propGetIndexByChrId(chr, chrnum);
		} else {
			struct chrdata *target = chrFindById(chr, chrnum);
			newtarget = target ? target->target : -1;
		}
		if (newtarget != chr->target) {
			chr->lastvisibletarget60 = 0;
			chr->lastseetarget60 = 0;
			chr->lastheartarget60 = 0;
			chr->hidden &= ~CHRHFLAG_IS_HEARING_TARGET;
			chr->chrflags &= ~CHRCFLAG_NEAR_MISS;
			chr->target = newtarget;
			applied = 1;
		}
	} else if (hovercar) {
		chopperSetTarget(hovercar, chrnum);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_target chr=%d mode=%d flags=%d target=%d applied=%d source=%s backend=graph.ai.action.prop_target+ai/ailists.tsv",
			chrnum, mode, flags, newtarget, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget(
	struct chrdata *chr, s32 label)
{
	s32 required;
	s32 preset_target = -1;
	s32 branch_taken;

	required = s_aiGraphRequirePropPresetTargetNode("condition",
		"if_presets_target_is_not_my_target",
		s_ActiveScenarioGraphs
			.level_ai_if_presets_target_is_not_my_target_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr && chr->chrpreset1 != -1) {
		preset_target = propGetIndexByChrId(chr, chr->chrpreset1);
	}
	branch_taken = chr && chr->target != -1 && preset_target != chr->target;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_presets_target_is_not_my_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_presets_target_is_not_my_target preset_target=%d target=%d label=%d branch=%d source=%s backend=graph.ai.condition.prop_target+ai/ailists.tsv",
			preset_target, chr ? chr->target : -1, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_presets_target_is_not_my_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf(
	struct chrdata *chr, s32 preset, f32 distance, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequirePropPresetTargetNode("action",
		"set_chr_preset_to_chr_near_self",
		s_ActiveScenarioGraphs
			.level_ai_set_chr_preset_to_chr_near_self_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chrSetChrPresetToChrNearSelf(preset, chr, distance);
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs
			.ai_action_set_chr_preset_to_chr_near_self_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_preset_to_chr_near_self preset=%d distance=%.3f label=%d branch=%d source=%s backend=graph.ai.action.prop_target+ai/ailists.tsv",
			preset, (double)distance, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_set_chr_preset_to_chr_near_self_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad(
	struct chrdata *chr, s32 preset, f32 distance, s32 padnum, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequirePropPresetTargetNode("action",
		"set_chr_preset_to_chr_near_pad",
		s_ActiveScenarioGraphs
			.level_ai_set_chr_preset_to_chr_near_pad_node_count,
		0, 1);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr &&
		chrSetChrPresetToChrNearPad(preset, chr, distance, padnum);
	s_aiGraphApplyBranch(branch_taken, label, 8);
	if (!s_ActiveScenarioGraphs
			.ai_action_set_chr_preset_to_chr_near_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_preset_to_chr_near_pad preset=%d distance=%.3f pad=%d label=%d branch=%d source=%s pads=%s backend=graph.ai.action.prop_target+ai/ailists.tsv+pads.tsv",
			preset, (double)distance, padnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs
			.ai_action_set_chr_preset_to_chr_near_pad_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireVehicleInvestigationNode(const char *kind,
	const char *name, s32 node_count, s32 require_objects, s32 require_pads)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai.%s.%s node",
			kind, name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			(require_objects && !s_ActiveScenarioGraphs.objects_path[0]) ||
			(require_pads && !s_ActiveScenarioGraphs.pads_path[0])) {
		s_aiGraphRuntimeFailure(name,
			require_pads
				? "missing ai/ailists.tsv, objects.tsv, or pads.tsv source"
				: require_objects
					? "missing ai/ailists.tsv or objects.tsv source"
					: "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDangerousObjectNearby(struct chrdata *chr,
	s32 flags, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireVehicleInvestigationNode("condition",
		"if_dangerous_object_nearby",
		s_ActiveScenarioGraphs
			.level_ai_if_dangerous_object_nearby_node_count,
		1, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chrDetectDangerousObject(chr, flags);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_dangerous_object_nearby_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_dangerous_object_nearby flags=%d label=%d branch=%d source=%s objects=%s backend=graph.ai.condition.vehicle_investigation+ai/ailists.tsv+objects.tsv",
			flags, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_dangerous_object_nearby_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHeliWeaponsArmed(
	struct chopperobj *hovercar, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireVehicleInvestigationNode("condition",
		"if_heli_weapons_armed",
		s_ActiveScenarioGraphs.level_ai_if_heli_weapons_armed_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = hovercar && hovercar->weaponsarmed;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_heli_weapons_armed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_heli_weapons_armed label=%d branch=%d source=%s backend=graph.ai.condition.vehicle_investigation+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_heli_weapons_armed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHoverbotNextStep(
	struct chopperobj *hovercar, s32 comparison, s32 value, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireVehicleInvestigationNode("condition",
		"if_hoverbot_next_step",
		s_ActiveScenarioGraphs.level_ai_if_hoverbot_next_step_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = hovercar &&
		((hovercar->nextstep > value && comparison == 1) ||
			(hovercar->nextstep < value && comparison == 0));
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs.ai_condition_if_hoverbot_next_step_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_hoverbot_next_step comparison=%d value=%d nextstep=%d label=%d branch=%d source=%s backend=graph.ai.condition.vehicle_investigation+ai/ailists.tsv",
			comparison, value, hovercar ? hovercar->nextstep : -1, label,
			branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_hoverbot_next_step_logged = 1;
	}
	return 1;
}

static void s_aiGraphCopyTagPlacement(s32 tag_id, s32 pc_id)
{
	struct tag *tag = tagFindById(tag_id);
	struct tag *pc = tagFindById(pc_id);

	if (!tag || !pc) {
		s_aiGraphRuntimeFailure("shuffle_investigation_terminals",
			"missing setup tag source");
		return;
	}
	tag->cmdoffset = pc->cmdoffset;
	tag->obj = pc->obj;
}

s32 scenarioSourceAiGraphExecuteShuffleInvestigationTerminals(s32 goodtag_id,
	s32 badtag_id, s32 pc1_id, s32 pc2_id, s32 pc3_id, s32 pc4_id,
	s32 enabled)
{
	s32 required;
	u8 rand1;
	u8 rand2;
	s32 good_pc;
	s32 bad_pc;

	required = s_aiGraphRequireVehicleInvestigationNode("action",
		"shuffle_investigation_terminals",
		s_ActiveScenarioGraphs
			.level_ai_shuffle_investigation_terminals_node_count,
		1, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (enabled == 0) {
		rand1 = rngRandom() % 3;
		rand2 = rngRandom() % 3;
		good_pc = rand1 == 0 ? pc1_id : rand1 == 1 ? pc2_id :
			rand1 == 2 ? pc3_id : pc4_id;
		if (rand2 == rand1 && rand2 > 0) {
			rand2--;
		} else if (rand2 == rand1 && rand2 < 3) {
			rand2++;
		}
		bad_pc = rand2 == 0 ? pc1_id : rand2 == 1 ? pc2_id :
			rand2 == 2 ? pc3_id : pc4_id;
		s_aiGraphCopyTagPlacement(goodtag_id, good_pc);
		s_aiGraphCopyTagPlacement(badtag_id, bad_pc);
	}
	g_Vars.aioffset += 9;
	if (!s_ActiveScenarioGraphs
			.ai_action_shuffle_investigation_terminals_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action shuffle_investigation_terminals enabled=%d source=%s objects=%s backend=graph.ai.action.vehicle_investigation+ai/ailists.tsv+objects.tsv",
			enabled, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs
			.ai_action_shuffle_investigation_terminals_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPadPresetToInvestigationTerminal(
	struct chrdata *chr, s32 tag_id, const u16 *pad_map, s32 pad_map_count)
{
	s32 required;
	struct defaultobj *obj;
	s32 applied = 0;
	s32 i;

	required = s_aiGraphRequireVehicleInvestigationNode("action",
		"set_pad_preset_to_investigation_terminal",
		s_ActiveScenarioGraphs
			.level_ai_set_pad_preset_to_investigation_terminal_node_count,
		1, 1);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj) {
		for (i = 0; i + 1 < pad_map_count; i += 2) {
			if (obj->pad == pad_map[i]) {
				chrSetPadPreset(chr, pad_map[i + 1]);
				applied = 1;
			}
		}
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_investigation_terminal_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_pad_preset_to_investigation_terminal tag=%d applied=%d source=%s objects=%s pads=%s backend=graph.ai.action.vehicle_investigation+ai/ailists.tsv+objects.tsv+pads.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_investigation_terminal_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteHeliSetWeaponsArmed(
	struct chopperobj *hovercar, s32 armed)
{
	s32 required;
	const char *name = armed ? "heli_arm_weapons" : "heli_unarm_weapons";
	s32 *logged = armed
		? &s_ActiveScenarioGraphs.ai_action_heli_arm_weapons_logged
		: &s_ActiveScenarioGraphs.ai_action_heli_unarm_weapons_logged;

	required = s_aiGraphRequireVehicleInvestigationNode("action", name,
		armed ? s_ActiveScenarioGraphs.level_ai_heli_arm_weapons_node_count
			: s_ActiveScenarioGraphs.level_ai_heli_unarm_weapons_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (hovercar) {
		chopperSetArmed(hovercar, armed);
	}
	g_Vars.aioffset += 2;
	if (!*logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action %s armed=%d source=%s backend=graph.ai.action.vehicle_investigation+ai/ailists.tsv",
			name, armed, s_ActiveScenarioGraphs.ai_lists_path);
		*logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireSafetyDetectionNode(const char *name,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai.condition.%s node",
			name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

static s32 s_aiGraphSafety2Score(struct chrdata *chr, s32 *out_nearby)
{
	s16 *chrnums;
	s32 score = 6;
	s32 numnearby = 0;

	if (!chr || !out_nearby) {
		if (out_nearby) {
			*out_nearby = 0;
		}
		return 0;
	}
	if (chrGetNumArghs(chr) > 0) {
		score -= 2;
	}
	switch (bgunGetWeaponNum(HAND_RIGHT)) {
	case WEAPON_FALCON2:
	case WEAPON_FALCON2_SILENCER:
	case WEAPON_FALCON2_SCOPE:
	case WEAPON_MAGSEC4:
	case WEAPON_MAULER:
	case WEAPON_PHOENIX:
	case WEAPON_DY357MAGNUM:
	case WEAPON_DY357LX:
	case WEAPON_CROSSBOW:
		break;
	case WEAPON_CMP150:
	case WEAPON_CYCLONE:
	case WEAPON_CALLISTO:
	case WEAPON_RCP120:
	case WEAPON_LAPTOPGUN:
	case WEAPON_DRAGON:
	case WEAPON_K7AVENGER:
	case WEAPON_AR34:
	case WEAPON_SUPERDRAGON:
	case WEAPON_SHOTGUN:
	case WEAPON_SNIPERRIFLE:
		score--;
		break;
	case WEAPON_REAPER:
	case WEAPON_FARSIGHT:
	case WEAPON_DEVASTATOR:
	case WEAPON_ROCKETLAUNCHER:
	case WEAPON_SLAYER:
		score -= 2;
		break;
	default:
		score++;
		break;
	}
	chrnums = teamGetChrIds(chr->team);
	for (; *chrnums != -2; chrnums++) {
		struct chrdata *nearby = chrFindByLiteralId(*chrnums);

		if (nearby && nearby->model && !chrIsDead(nearby) &&
				nearby->actiontype != ACT_DEAD &&
				nearby->alertness > 100 &&
				chr->squadron == nearby->squadron &&
				chr->chrnum != nearby->chrnum &&
				chrGetDistanceToChr(chr, nearby->chrnum) < 3500) {
			numnearby++;
		}
	}
	if (numnearby == 0) {
		score -= 2;
	} else if (numnearby == 1) {
		score--;
	}
	if (score < 3 && numnearby != 0) {
		score = 3;
	}
	*out_nearby = numnearby;
	return score;
}

s32 scenarioSourceAiGraphExecuteIfSafety2LessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 required;
	s32 nearby;
	s32 score;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("if_safety2_less_than",
		s_ActiveScenarioGraphs.level_ai_if_safety2_less_than_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	score = s_aiGraphSafety2Score(chr, &nearby);
	branch_taken = chr && score < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_safety2_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_safety2_less_than threshold=%d score=%d nearby=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			threshold, score, nearby, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_safety2_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34(s32 label)
{
	s32 required;
	s32 weaponnum;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode(
		"if_player_using_cmp_or_ar34",
		s_ActiveScenarioGraphs
			.level_ai_if_player_using_cmp_or_ar34_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	weaponnum = bgunGetWeaponNum(HAND_RIGHT);
	branch_taken = weaponnum == WEAPON_CMP150 || weaponnum == WEAPON_AR34;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_player_using_cmp_or_ar34_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_using_cmp_or_ar34 weapon=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			weaponnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_player_using_cmp_or_ar34_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor(struct chrdata *chr,
	s32 label)
{
	s32 required;
	s32 team = 0;
	f32 closestdist = 9999.9f;
	f32 scandist;
	f32 y;
	s16 *chrnums;
	s16 newtarget = -1;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode(
		"detect_enemy_on_same_floor",
		s_ActiveScenarioGraphs
			.level_ai_detect_enemy_on_same_floor_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (!chr || !chr->prop) {
		s_aiGraphApplyBranch(0, label, 3);
		return 1;
	}
	if (chr->teamscandist == 0) {
		scandist = 1500;
	} else if (chr->teamscandist == 255) {
		scandist = 9999;
	} else {
		scandist = chr->teamscandist * 40.0f;
	}
	y = chr->prop->pos.y;
	chrnums = teamGetChrIds(1);
	while (team < 8) {
		struct chrdata *candidate = chrFindByLiteralId(*chrnums);

		if (*chrnums != -2) {
			if (candidate && candidate->prop &&
					candidate->team != TEAM_NONCOMBAT &&
					!chrIsDead(candidate) &&
					candidate->actiontype != ACT_DEAD &&
					candidate->actiontype != ACT_DRUGGEDKO &&
					candidate->actiontype != ACT_DRUGGEDDROP &&
					candidate->actiontype != ACT_DRUGGEDCOMINGUP &&
					chrCompareTeams(chr, candidate, COMPARE_ENEMIES) &&
					(candidate->hidden & CHRHFLAG_CLOAKED) == 0 &&
					(candidate->chrflags & CHRCFLAG_HIDDEN) == 0 &&
					(candidate->hidden & CHRHFLAG_ANTINONINTERACTABLE) == 0 &&
					y - candidate->prop->pos.y > -200 &&
					y - candidate->prop->pos.y < 200 &&
					((chr->hidden & CHRHFLAG_PSYCHOSISED) == 0 ||
						(candidate->hidden & CHRHFLAG_ANTINONINTERACTABLE) == 0 ||
						(candidate->hidden & CHRHFLAG_DONTSHOOTME)) &&
					chr->chrnum != candidate->chrnum) {
				f32 distance = chrGetDistanceToChr(chr, candidate->chrnum);

				if (distance < closestdist &&
						(distance < scandist ||
							stageGetIndex(g_Vars.stagenum) == STAGEINDEX_MAIANSOS)) {
					closestdist = distance;
					newtarget = candidate->chrnum;
				}
			}
			chrnums++;
		} else {
			chrnums++;
			team++;
		}
	}
	branch_taken = newtarget != -1;
	if (branch_taken) {
		chr->target = propGetIndexByChrId(chr, newtarget);
	}
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_detect_enemy_on_same_floor_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition detect_enemy_on_same_floor scan=%.3f target=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			(double)scandist, newtarget, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_detect_enemy_on_same_floor_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDetectEnemy(struct chrdata *chr,
	s32 maxdist_raw, s32 label)
{
	s32 required;
	s16 *chrnums;
	s32 team = 0;
	f32 closestdist = 10000000;
	f32 maxdist = maxdist_raw * 10.0f;
	s16 closesttarg = -1;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("detect_enemy",
		s_ActiveScenarioGraphs.level_ai_detect_enemy_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (!chr) {
		s_aiGraphApplyBranch(0, label, 4);
		return 1;
	}
	chrnums = teamGetChrIds(1);
	do {
		u8 teamvalue = (1 << team);

		while (*chrnums != -2 && chr->team != teamvalue) {
			struct chrdata *candidate = chrFindByLiteralId(*chrnums);

			if (candidate && candidate->prop && !chrIsDead(candidate) &&
					candidate->actiontype != ACT_DEAD &&
					candidate->actiontype != ACT_DIE &&
					candidate->actiontype != ACT_DRUGGEDKO &&
					candidate->actiontype != ACT_DRUGGEDDROP &&
					candidate->actiontype != ACT_DRUGGEDCOMINGUP &&
					chrCompareTeams(chr, candidate, COMPARE_ENEMIES) &&
					candidate != chr &&
					(candidate->hidden & CHRHFLAG_CLOAKED) == 0 &&
					(candidate->chrflags & CHRCFLAG_HIDDEN) == 0 &&
					(candidate->hidden & CHRHFLAG_DISGUISED) == 0 &&
					candidate->team != TEAM_NONCOMBAT &&
					((chr->hidden & CHRHFLAG_PSYCHOSISED) == 0 ||
						(candidate->hidden & CHRHFLAG_ANTINONINTERACTABLE) == 0 ||
						(candidate->hidden & CHRHFLAG_DONTSHOOTME))) {
				f32 distance = chrGetDistanceToChr(chr, candidate->chrnum);

				if (distance < maxdist && distance != 0 &&
						distance < closestdist &&
						chrHasLosToProp(chr, candidate->prop) &&
						(candidate->chrflags & CHRCFLAG_HIDDEN) == 0) {
					if (chr->yvisang == 0) {
						closestdist = distance;
						closesttarg = candidate->chrnum;
					} else {
						s16 prevtarget = chr->target;
						chr->target = propGetIndexByChrId(chr,
							candidate->chrnum);
						if (chrIsVerticalAngleToTargetWithin(chr,
								chr->yvisang)) {
							closestdist = distance;
							closesttarg = candidate->chrnum;
						}
						chr->target = prevtarget;
					}
				}
			}
			chrnums++;
		}
		if (*chrnums == -2) {
			team++;
		}
		chrnums++;
	} while (team < 8);
	branch_taken = closesttarg != -1;
	if (branch_taken) {
		chr->target = propGetIndexByChrId(chr, closesttarg);
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_detect_enemy_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition detect_enemy maxdist=%.3f target=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			(double)maxdist, closesttarg, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_detect_enemy_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSafetyLessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 required;
	s16 *chrnums;
	s32 safety = 6;
	s32 numnearby = 0;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("if_safety_less_than",
		s_ActiveScenarioGraphs.level_ai_if_safety_less_than_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr) {
		if (chrGetNumArghs(chr) > 0) {
			safety--;
		}
		chrnums = teamGetChrIds(chr->team);
		for (; *chrnums != -2; chrnums++) {
			struct chrdata *nearby = chrFindByLiteralId(*chrnums);

			if (nearby && nearby->model && !chrIsDead(nearby) &&
					nearby->actiontype != ACT_DEAD &&
					chr->chrnum != nearby->chrnum &&
					chrGetDistanceToChr(chr, nearby->chrnum) < 3500) {
				numnearby++;
			}
		}
		if (numnearby == 0) {
			safety -= 2;
		} else if (numnearby < 3) {
			safety--;
		}
	}
	branch_taken = chr && safety < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_safety_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_safety_less_than threshold=%d safety=%d nearby=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			threshold, safety, numnearby, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_safety_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTargetMovingSlowly(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	s32 required;
	struct chrdata *chr;
	s32 delta = 0;
	s32 absdelta;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("if_target_moving_slowly",
		s_ActiveScenarioGraphs.level_ai_if_target_moving_slowly_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chr = chrnum == 0 ? basechr : chrFindById(basechr, chrnum);
	if (chr) {
		delta = chrGetDistanceLostToTargetInLastSecond(chr);
	}
	absdelta = delta > 0 ? delta : -delta;
	branch_taken = chr && absdelta < 50;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_slowly_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_target_moving_slowly chr=%d delta=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			chrnum, delta, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_slowly_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTargetMovingCloser(struct chrdata *chr,
	s32 label)
{
	s32 required;
	s32 delta = 0;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("if_target_moving_closer",
		s_ActiveScenarioGraphs.level_ai_if_target_moving_closer_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr) {
		delta = chrGetDistanceLostToTargetInLastSecond(chr);
	}
	branch_taken = chr && delta < -50;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_closer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_target_moving_closer delta=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			delta, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_closer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTargetMovingAway(struct chrdata *chr,
	s32 label)
{
	s32 required;
	s32 delta = 0;
	s32 branch_taken;

	required = s_aiGraphRequireSafetyDetectionNode("if_target_moving_away",
		s_ActiveScenarioGraphs.level_ai_if_target_moving_away_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr) {
		delta = chrGetDistanceLostToTargetInLastSecond(chr);
	}
	branch_taken = chr && delta > 50;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_away_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_target_moving_away delta=%d label=%d branch=%d source=%s backend=graph.ai.condition.safety_detection+ai/ailists.tsv",
			delta, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_target_moving_away_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireMiscBranchNode(const char *name, s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai.condition.%s node",
			name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSquadronIsDead(s32 squadron, s32 label)
{
	s32 required;
	bool anyalive = true;
	s16 *chrnums;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode("if_squadron_is_dead",
		s_ActiveScenarioGraphs.level_ai_if_squadron_is_dead_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chrnums = squadronGetChrIds(squadron);
	if (chrnums) {
		while (*chrnums != -2) {
			struct chrdata *chr = chrFindByLiteralId(*chrnums);

			if (chr && chr->model) {
				anyalive = false;
				if (!chrIsDead(chr) && chr->actiontype != ACT_DEAD) {
					anyalive = true;
				}
			}
			chrnums++;
		}
	}
	branch_taken = !anyalive;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_squadron_is_dead_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_squadron_is_dead squadron=%d alive=%d label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			squadron, anyalive, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_squadron_is_dead_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTrue(s32 label)
{
	s32 required;

	required = s_aiGraphRequireMiscBranchNode("if_true",
		s_ActiveScenarioGraphs.level_ai_if_true_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	s_aiGraphApplyBranch(1, label, 6);
	if (!s_ActiveScenarioGraphs.ai_condition_if_true_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_true label=%d branch=1 source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			label, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_true_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan(s32 threshold,
	s32 squadron, s32 label)
{
	s32 required;
	s32 count = 0;
	s16 *chrnums;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode(
		"if_num_chrs_in_squadron_greater_than",
		s_ActiveScenarioGraphs
			.level_ai_if_num_chrs_in_squadron_greater_than_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chrnums = squadronGetChrIds(squadron);
	if (chrnums) {
		while (*chrnums != -2) {
			struct chrdata *chr = chrFindByLiteralId(*chrnums);

			if (chr && chr->prop && chrIsDead(chr) == false &&
					chr->actiontype != ACT_DEAD &&
					chr->actiontype != ACT_DRUGGEDKO &&
					chr->actiontype != ACT_DRUGGEDDROP &&
					chr->actiontype != ACT_DRUGGEDCOMINGUP) {
				count++;
			}
			chrnums++;
		}
	}
	branch_taken = count > threshold;
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_chrs_in_squadron_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_chrs_in_squadron_greater_than threshold=%d squadron=%d count=%d label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			threshold, squadron, count, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_chrs_in_squadron_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNaturalAnim(struct chrdata *chr,
	s32 anim, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode("if_natural_anim",
		s_ActiveScenarioGraphs.level_ai_if_natural_anim_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chr->naturalanim == anim;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_natural_anim_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_natural_anim anim=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			anim, chr ? chr->naturalanim : -1, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_natural_anim_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfY(struct chrdata *basechr, s32 chrnum,
	s32 cutoff_y, s32 comparison, s32 label)
{
	s32 required;
	struct chrdata *chr = NULL;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode("if_y",
		s_ActiveScenarioGraphs.level_ai_if_y_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chrnum == CHR_TARGET && g_Vars.hovercar) {
		struct chopperobj *chopper = chopperFromHovercar(g_Vars.hovercar);

		if (chopper) {
			struct prop *target = chopperGetTargetProp(chopper);

			if (target && (target->type == PROPTYPE_CHR ||
					target->type == PROPTYPE_PLAYER)) {
				chr = target->chr;
			}
		}
	} else {
		chr = chrFindById(basechr, chrnum);
	}
	branch_taken = chr && chr->prop &&
		((chr->prop->pos.y < cutoff_y && comparison == 0) ||
			(chr->prop->pos.y > cutoff_y && comparison == 1));
	s_aiGraphApplyBranch(branch_taken, label, 7);
	if (!s_ActiveScenarioGraphs.ai_condition_if_y_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_y chr=%d y=%.3f cutoff=%d comparison=%d label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			chrnum, chr && chr->prop ? (double)chr->prop->pos.y : 0.0,
			cutoff_y, comparison, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_y_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSoundTimer(struct chrdata *chr,
	s32 ticks_value, s32 comparison, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode("if_sound_timer",
		s_ActiveScenarioGraphs.level_ai_if_sound_timer_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr &&
		((chr->soundtimer > ticks_value && comparison == 0) ||
			(chr->soundtimer < ticks_value && comparison == 1));
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs.ai_condition_if_sound_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_sound_timer value=%d timer=%d comparison=%d label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			ticks_value, chr ? chr->soundtimer : -1, comparison, label,
			branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_sound_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 required;
	struct prop *prop;
	f32 diff = 0;
	s32 branch_taken;

	required = s_aiGraphRequireMiscBranchNode(
		"if_target_y_difference_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_target_y_difference_less_than_node_count);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	prop = chr ? chrGetTargetProp(chr) : NULL;
	if (chr && chr->prop && prop) {
		diff = prop->pos.y - chr->prop->pos.y;
		if (diff < 0) {
			diff = 0 - diff;
		}
	}
	branch_taken = chr && chr->prop && prop && diff < threshold * 10.0f;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_target_y_difference_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_target_y_difference_less_than threshold=%d diff=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.misc_branch+ai/ailists.tsv",
			threshold, (double)diff, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_target_y_difference_less_than_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireMiscEffectNode(const char *name, s32 node_count,
	s32 require_objects, s32 require_pads)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai action/condition.%s node",
			name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing ai/ailists.tsv source");
		return -1;
	}
	if (require_objects && !s_ActiveScenarioGraphs.objects_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing objects.tsv source");
		return -1;
	}
	if (require_pads && !s_ActiveScenarioGraphs.pads_path[0]) {
		s_aiGraphRuntimeFailure(name, "missing pads.tsv source");
		return -1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrExplosions(struct chrdata *basechr,
	s32 chrnum)
{
	s32 required;
	struct chrdata *chr;

	required = s_aiGraphRequireMiscEffectNode("chr_explosions",
		s_ActiveScenarioGraphs.level_ai_chr_explosions_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		playerSurroundWithExplosions(0);
		setCurrentPlayerNum(prevplayernum);
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_chr_explosions_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_explosions chr=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			chrnum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_explosions_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetTintedGlassEnabled(s32 enabled)
{
	s32 required;

	required = s_aiGraphRequireMiscEffectNode("set_tinted_glass_enabled",
		s_ActiveScenarioGraphs
			.level_ai_set_tinted_glass_enabled_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	g_TintedGlassEnabled = enabled;
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_set_tinted_glass_enabled_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_tinted_glass_enabled enabled=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			enabled, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_tinted_glass_enabled_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteHovercopterFireRocket(s32 side)
{
	s32 required;

	required = s_aiGraphRequireMiscEffectNode("hovercopter_fire_rocket",
		s_ActiveScenarioGraphs
			.level_ai_hovercopter_fire_rocket_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chopperFireRocket(g_Vars.hovercar, side);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_hovercopter_fire_rocket_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action hovercopter_fire_rocket side=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			side, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_hovercopter_fire_rocket_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrAdjustMotionBlur(struct chrdata *basechr,
	s32 chrnum, s32 amount, s32 mode)
{
	s32 required;
	struct chrdata *chr;

	required = s_aiGraphRequireMiscEffectNode("chr_adjust_motion_blur",
		s_ActiveScenarioGraphs.level_ai_chr_adjust_motion_blur_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		if (mode == 0) {
			chr->blurdrugamount -= TICKS(amount);
		} else {
			chr->blurdrugamount += TICKS(amount);
		}
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_chr_adjust_motion_blur_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_adjust_motion_blur chr=%d amount=%d mode=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			chrnum, amount, mode, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_adjust_motion_blur_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePunchOrKick(struct chrdata *chr, s32 reverse,
	s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireMiscEffectNode("punch_or_kick",
		s_ActiveScenarioGraphs.level_ai_punch_or_kick_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chrTryPunch(chr, reverse);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_action_punch_or_kick_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action punch_or_kick reverse=%d label=%d branch=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			reverse, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_punch_or_kick_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight(
	struct chrdata *chr, s32 label)
{
	s32 required;
	s16 prevtarget;
	s32 branch_taken = 0;

	required = s_aiGraphRequireMiscEffectNode(
		"set_target_to_eyespy_if_in_sight",
		s_ActiveScenarioGraphs
			.level_ai_set_target_to_eyespy_if_in_sight_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (chr) {
		struct eyespy *eyespy = g_Vars.players[chr->p1p2]->eyespy;
		prevtarget = chr->target;
		if (eyespy) {
			struct chrdata *targetchr = eyespy->prop->chr;
			chr->target = propGetIndexByChrId(chr, targetchr->chrnum);
			if (chrCheckCanSeeTarget(chr)) {
				branch_taken = 1;
			} else {
				chr->target = prevtarget;
			}
		}
	}
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_action_set_target_to_eyespy_if_in_sight_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_target_to_eyespy_if_in_sight label=%d branch=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_set_target_to_eyespy_if_in_sight_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteMiniSkedarTryPounce(struct chrdata *chr,
	s32 arg0, s32 arg1, s32 arg2, s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireMiscEffectNode("mini_skedar_try_pounce",
		s_ActiveScenarioGraphs.level_ai_mini_skedar_try_pounce_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = chr && chrTrySkJump(chr, chr->pouncebits, arg0, arg1,
		arg2);
	s_aiGraphApplyBranch(branch_taken, label, 7);
	if (!s_ActiveScenarioGraphs.ai_action_mini_skedar_try_pounce_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action mini_skedar_try_pounce arg0=%d arg1=%d arg2=%d label=%d branch=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			arg0, arg1, arg2, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_mini_skedar_try_pounce_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan(
	struct chrdata *chr, s32 tag_id, f32 distance, s32 pad_id, s32 label)
{
	s32 required;
	struct defaultobj *obj;
	struct pad pad;
	s32 branch_taken = 0;

	required = s_aiGraphRequireMiscEffectNode(
		"if_object_distance_to_pad_less_than",
		s_ActiveScenarioGraphs
			.level_ai_if_object_distance_to_pad_less_than_node_count, 1, 1);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		pad_id = chrResolvePadId(chr, pad_id);
		if (pad_id >= 0) {
			f32 xdiff;
			f32 ydiff;
			f32 zdiff;
			padUnpack(pad_id, PADFIELD_POS, &pad);
			xdiff = obj->prop->pos.x - pad.pos.x;
			ydiff = obj->prop->pos.y - pad.pos.y;
			zdiff = obj->prop->pos.z - pad.pos.z;
			branch_taken = ydiff < 200 && ydiff > -200 &&
				xdiff < distance && xdiff > -distance &&
				zdiff < distance && zdiff > -distance;
		}
	}
	s_aiGraphApplyBranch(branch_taken, label, 8);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_object_distance_to_pad_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_object_distance_to_pad_less_than tag=%d pad=%d distance=%.3f label=%d branch=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv+objects.tsv+pads.tsv",
			tag_id, pad_id, (double)distance, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_object_distance_to_pad_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAvoid(struct chrdata *chr)
{
	s32 required;

	required = s_aiGraphRequireMiscEffectNode("avoid",
		s_ActiveScenarioGraphs.level_ai_avoid_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chrAvoid(chr);
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_avoid_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action avoid source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_avoid_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTitleInitMode(s32 mode)
{
	s32 required;

	required = s_aiGraphRequireMiscEffectNode("title_init_mode",
		s_ActiveScenarioGraphs.level_ai_title_init_mode_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	g_Vars.aioffset += 3;
	titleInitFromAiCmd(mode);
	if (!s_ActiveScenarioGraphs.ai_action_title_init_mode_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action title_init_mode mode=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			mode, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_title_init_mode_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryExitTitle(s32 label)
{
	s32 required;
	s32 branch_taken;

	required = s_aiGraphRequireMiscEffectNode("try_exit_title",
		s_ActiveScenarioGraphs.level_ai_try_exit_title_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	branch_taken = titleIsChangingMode();
	if (branch_taken) {
		titleExit();
	}
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_action_try_exit_title_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_exit_title label=%d branch=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_try_exit_title_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrEmitSparks(struct chrdata *basechr,
	s32 chrnum)
{
	s32 required;
	struct chrdata *chr;

	required = s_aiGraphRequireMiscEffectNode("chr_emit_sparks",
		s_ActiveScenarioGraphs.level_ai_chr_emit_sparks_node_count, 0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		chrDrCarollEmitSparks(chr);
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_chr_emit_sparks_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_emit_sparks chr=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			chrnum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_emit_sparks_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetDrCarollImages(struct chrdata *basechr,
	s32 chrnum, s32 right_image, s32 left_image)
{
	s32 required;
	struct chrdata *drcaroll;

	required = s_aiGraphRequireMiscEffectNode("set_dr_caroll_images",
		s_ActiveScenarioGraphs.level_ai_set_dr_caroll_images_node_count,
		0, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	drcaroll = chrFindById(basechr, chrnum);
	if (drcaroll) {
		if (left_image == 7) {
			if ((g_Vars.lvframenum % 4) == 2) {
				drcaroll->drcarollimage_left = rngRandom() % 6;
			}
		} else if (left_image == 8) {
			drcaroll->drcarollimage_left = rngRandom() % 6;
		} else {
			drcaroll->drcarollimage_left = left_image;
		}

		if (right_image == 7) {
			if ((g_Vars.lvframenum % 4) == 2) {
				drcaroll->drcarollimage_right = rngRandom() % 6;
			}
		} else if (right_image == 8) {
			drcaroll->drcarollimage_right = rngRandom() % 6;
		} else {
			drcaroll->drcarollimage_right = right_image;
		}
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_set_dr_caroll_images_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_dr_caroll_images chr=%d right=%d left=%d source=%s backend=graph.ai.action.misc_effect+ai/ailists.tsv",
			chrnum, right_image, left_image,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_dr_caroll_images_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireQuipShuffleNode(const char *name, s32 node_count,
	s32 require_objects)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason), "missing scenario.ai.action.%s node",
			name);
		s_aiGraphRuntimeFailure(name, reason);
		return -1;
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			(require_objects && !s_ActiveScenarioGraphs.objects_path[0])) {
		s_aiGraphRuntimeFailure(name,
			require_objects
				? "missing ai/ailists.tsv or objects.tsv source"
				: "missing ai/ailists.tsv source");
		return -1;
	}
	return 1;
}

static void s_aiGraphPlayQuipSound(struct chrdata *chr, s16 audioid)
{
	s32 distance;

	if (!chr || !chr->prop) {
		return;
	}
	if (!AI_QUIP_AUDIO_NEEDS_MOVEMENT(audioid)) {
		psStopSound(chr->prop, PSTYPE_CHRTALK, 0xffff);
		psCreate(0, chr->prop, audioid, -1,
			-1, PSFLAG_FORPROP, 0, PSTYPE_CHRTALK, 0, -1, 0, -1, -1,
			-1, -1);
		return;
	}
	distance = chrGetDistanceLostToTargetInLastSecond(chr);
	if (ABS(distance) > 50) {
		psStopSound(chr->prop, PSTYPE_CHRTALK, 0xffff);
		psCreate(0, chr->prop, audioid, -1,
			-1, PSFLAG_FORPROP, 0, PSTYPE_CHRTALK, 0, -1, 0, -1, -1,
			-1, -1);
	}
}

s32 scenarioSourceAiGraphExecuteSayQuip(struct chrdata *basechr, s32 chr_id,
	s32 row_arg, s32 probability_arg, s32 sound_gap, s32 nearby_mode,
	s32 quip_flags, s32 text_index, s32 colour)
{
	s32 required;
	u8 column;
	s16 audioid;
	u8 i;
	s32 numnearbychrs;
	bool issomeonetalking;
	s32 probability;
	s16 *rowptr;
	s16 *chrnums;
	s16 *bank;
	char *text;
	struct chrdata *chr;
	u32 prevplayernum;
	u32 playernum;
	s32 row;
#ifdef __sgi
	u8 headshotted;
#else
	u8 headshotted = 0;
#endif
	struct chrdata *loopchr;

	required = s_aiGraphRequireQuipShuffleNode("say_quip",
		s_ActiveScenarioGraphs.level_ai_say_quip_node_count, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	chr = chrFindById(basechr, chr_id);
	prevplayernum = g_Vars.currentplayernum;
	row = row_arg;
#ifdef __sgi
	headshotted = (g_Vars.chrdata->hidden2 & CHRH2FLAG_HEADSHOTTED);
#endif

	if (CHRRACE(g_Vars.chrdata) == RACE_SKEDAR) {
		bank = (s16 *)g_SkedarQuipBank;
		if (row > 5) {
			row = 0;
		}
	} else if (g_Vars.chrdata->headnum == HEAD_MAIAN_S) {
		bank = (s16 *)g_MaianQuipBank;
		if (row > 2) {
			row = rngRandom() % 2;
		}
	} else if (quip_flags == 0) {
		if (g_Vars.chrdata->voicebox > 3) {
			g_Vars.chrdata->voicebox = 3;
		}
		bank = (s16 *)g_GuardQuipBank[g_Vars.chrdata->voicebox * 41];
	} else {
		bank = (s16 *)g_SpecialQuipBank;
	}

	if (!row && !probability_arg && !nearby_mode) {
		g_Vars.chrdata->soundtimer = 0;
		g_Vars.aioffset += 10;
		return 1;
	}

	chrnums = teamGetChrIds(g_Vars.chrdata->team);
	numnearbychrs = 0;
	issomeonetalking = false;
	probability = probability_arg;

	if ((g_Vars.chrdata->headnum == HEAD_ELVIS ||
				g_Vars.chrdata->headnum == HEAD_THEKING ||
				g_Vars.chrdata->headnum == HEAD_ELVIS_GOGS ||
				g_Vars.chrdata->headnum == HEAD_JONATHAN) &&
			bank != (s16 *)g_SpecialQuipBank) {
		probability = 0;
	}

	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		playernum = playermgrGetPlayerNumByProp(chr->prop);
		if (g_Vars.coopplayernum >= 0 && g_Vars.players[playernum]->isdead) {
			playernum = playernum == g_Vars.bondplayernum
				? g_Vars.coopplayernum : g_Vars.bondplayernum;
		}
		setCurrentPlayerNum(playernum);
	}

	if (g_Vars.chrdata->soundgap == 0 ||
			g_Vars.chrdata->soundgap * TICKS(60) <
			g_Vars.chrdata->soundtimer) {
		if (probability > (s32)(rngRandom() % 256)) {
			while (*chrnums != -2) {
				loopchr = chrFindByLiteralId(*chrnums);
				if (loopchr && loopchr->model &&
						!chrIsDead(loopchr) &&
						loopchr->actiontype != ACT_DEAD &&
						g_Vars.chrdata->squadron == loopchr->squadron &&
						loopchr->alertness >= 100 &&
						g_Vars.chrdata->chrnum != loopchr->chrnum &&
						chrGetDistanceToChr(g_Vars.chrdata,
							loopchr->chrnum) < 7000) {
					numnearbychrs++;
					if (loopchr->soundtimer < TICKS(60) &&
							nearby_mode != 0 && nearby_mode != 255) {
						issomeonetalking = true;
					}
				}
				chrnums++;
			}

			if (!issomeonetalking &&
					((numnearbychrs == 0 &&
						(!nearby_mode || nearby_mode == 255)) ||
					(numnearbychrs > 0 && nearby_mode > 0))) {
				rowptr = (s16 *)bank + row * 4;
				column = rngRandom() % 3;
				if ((quip_flags & 0x80) == 0) {
					audioid = rowptr[1 + column];
				} else {
					audioid = rowptr[1 + g_Vars.chrdata->tude];
				}

				if (audioWasNotPlayedRecently(audioid) ||
						CHRRACE(g_Vars.chrdata) == RACE_SKEDAR) {
					audioMarkAsRecentlyPlayed(audioid);
					if (audioid == SFX_M1_CHOKING && !headshotted) {
						audioid = SFX_M1_WHY_ME;
					}
					g_Vars.chrdata->soundtimer = 0;
					g_Vars.chrdata->soundgap = sound_gap;
					g_Vars.chrdata->propsoundcount++;
					s_aiGraphPlayQuipSound(g_Vars.chrdata, audioid);
					if (text_index && (quip_flags & 0x80) == 0) {
						if (column > 2) {
							column = 2;
						}
						text = langGet(g_QuipTexts[text_index - 1][1 + column]);
						if (!sndIsFiltered(audioid)) {
							hudmsgCreateWithColour(text,
								HUDMSGTYPE_INGAMESUBTITLE, colour);
						}
					} else if (text_index) {
						text = langGet(g_QuipTexts[text_index - 1]
							[1 + g_Vars.chrdata->tude]);
						if (!sndIsFiltered(audioid)) {
							hudmsgCreateWithColour(text,
								HUDMSGTYPE_INGAMESUBTITLE, colour);
						}
					}
				} else {
					audioid = 0;
					for (i = 1; i < ARRAYCOUNT(g_GuardQuipBank[row]); i++) {
						if (audioWasNotPlayedRecently(g_GuardQuipBank[row][i]) &&
								audioWasNotPlayedRecently(rowptr[i])) {
							audioid = rowptr[i];
							break;
						}
					}
					if (audioid) {
						audioMarkAsRecentlyPlayed(audioid);
						if (audioid == SFX_M1_CHOKING && !headshotted) {
							audioid = SFX_M1_WHY_ME;
						}
						g_Vars.chrdata->soundtimer = 0;
						g_Vars.chrdata->soundgap = sound_gap;
						g_Vars.chrdata->propsoundcount++;
						s_aiGraphPlayQuipSound(g_Vars.chrdata, audioid);
						if (text_index) {
							text = langGet(g_QuipTexts[text_index - 1][i]);
							if (!sndIsFiltered(audioid)) {
								hudmsgCreateWithColour(text,
									HUDMSGTYPE_INGAMESUBTITLE, colour);
							}
						}
					} else {
						g_Vars.chrdata->soundtimer = 0;
						g_Vars.chrdata->soundgap = sound_gap;
						chrUnsetFlags(g_Vars.chrdata,
							CHRFLAG1_TALKINGTODISGUISE, BANK_1);
					}
				}
			}
		}
	}

	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 10;
	if (!s_ActiveScenarioGraphs.ai_action_say_quip_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action say_quip chr=%d row=%d text=%d source=%s backend=graph.ai.action.quip_shuffle+ai/ailists.tsv",
			chr_id, row_arg, text_index,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_say_quip_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSayCiStaffQuip(struct chrdata *chr,
	s32 quip_type, s32 channel)
{
	s32 required;
	s16 quip = 0;

	required = s_aiGraphRequireQuipShuffleNode("say_ci_staff_quip",
		s_ActiveScenarioGraphs.level_ai_say_ci_staff_quip_node_count, 0);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("say_ci_staff_quip", "missing chr");
	}
	if (quip_type == CIQUIP_GREETING) {
		quip = g_CiGreetingQuips[chr->morale][rngRandom() % 3];
		psPlayFromProp((s8)channel, quip, 0, chr->prop, PSTYPE_CHRTALK, 0);
	}
	if (quip_type == CIQUIP_MAIN) {
		quip = g_CiMainQuips[chr->morale][rngRandom() % 3];
		psPlayFromProp((s8)channel, quip, 0, chr->prop, PSTYPE_CHRTALK, 0);
	}
	if (quip_type == CIQUIP_ANNOYED) {
		quip = g_CiAnnoyedQuips[chr->morale][rngRandom() % 3];
		psPlayFromProp((s8)channel, quip, 0, chr->prop, PSTYPE_CHRTALK, 0);
	}
	if (quip_type == CIQUIP_THANKS) {
		quip = g_CiThanksQuips[chr->morale];
		psPlayFromProp((s8)channel, quip, 0, chr->prop, PSTYPE_CHRTALK, 0);
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_say_ci_staff_quip_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action say_ci_staff_quip type=%d channel=%d quip=%d source=%s backend=graph.ai.action.quip_shuffle+ai/ailists.tsv",
			quip_type, channel, quip,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_say_ci_staff_quip_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphCopyTagPlacementChecked(const char *action,
	s32 dst_tag_id, s32 src_tag_id)
{
	struct tag *dst = tagFindById(dst_tag_id);
	struct tag *src = tagFindById(src_tag_id);

	if (!dst || !src) {
		return s_aiGraphRuntimeFailure(action, "missing setup tag source");
	}
	dst->cmdoffset = src->cmdoffset;
	dst->obj = src->obj;
	return 0;
}

s32 scenarioSourceAiGraphExecuteShuffleRuinsPillars(const u8 *cmd)
{
	s32 required;
	u8 marked1index;
	u8 marked2index;
	u8 marked3index;
	u8 pillars[5];
	u8 mines[5];

	required = s_aiGraphRequireQuipShuffleNode("shuffle_ruins_pillars",
		s_ActiveScenarioGraphs.level_ai_shuffle_ruins_pillars_node_count,
		1);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	pillars[0] = cmd[5];
	pillars[1] = cmd[6];
	pillars[2] = cmd[7];
	pillars[3] = cmd[8];
	pillars[4] = cmd[9];
	mines[0] = cmd[13];
	mines[1] = cmd[14];
	mines[2] = cmd[15];
	mines[3] = cmd[16];
	mines[4] = cmd[17];
	marked1index = rngRandom() % 5;
	marked2index = rngRandom() % 5;
	marked3index = rngRandom() % 5;
	while (marked2index == marked1index) {
		marked2index = rngRandom() % 5;
	}
	while (marked3index == marked2index || marked3index == marked1index) {
		marked3index = rngRandom() % 5;
	}
	if (s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
			cmd[2], pillars[marked1index]) ||
			s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
				cmd[10], mines[marked1index]) ||
			s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
				cmd[3], pillars[marked2index]) ||
			s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
				cmd[11], mines[marked2index]) ||
			s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
				cmd[4], pillars[marked3index]) ||
			s_aiGraphCopyTagPlacementChecked("shuffle_ruins_pillars",
				cmd[12], mines[marked3index])) {
		g_Vars.aioffset += 18;
		return 1;
	}
	g_Vars.aioffset += 18;
	if (!s_ActiveScenarioGraphs.ai_action_shuffle_ruins_pillars_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action shuffle_ruins_pillars choices=%u/%u/%u source=%s objects=%s backend=graph.ai.action.quip_shuffle+ai/ailists.tsv+objects.tsv",
			(unsigned)marked1index, (unsigned)marked2index,
			(unsigned)marked3index, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_shuffle_ruins_pillars_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteShufflePelagicSwitches(void)
{
	s32 required;
	u8 buttonsdone[] = {0, 0, 0, 0, 0, 0, 0, 0};
	u8 i;
	u8 j;
	u8 index;

	required = s_aiGraphRequireQuipShuffleNode("shuffle_pelagic_switches",
		s_ActiveScenarioGraphs
			.level_ai_shuffle_pelagic_switches_node_count,
		1);
	if (required <= 0) {
		return required < 0 ? 1 : 0;
	}
	for (i = 8; i < 16; i++) {
		index = rngRandom() & 7;
		if (buttonsdone[index] == 0) {
			if (s_aiGraphCopyTagPlacementChecked(
					"shuffle_pelagic_switches", i, index)) {
				g_Vars.aioffset += 2;
				return 1;
			}
			buttonsdone[index] = 1;
		} else {
			for (j = 0; buttonsdone[j]; j++);
			if (s_aiGraphCopyTagPlacementChecked(
					"shuffle_pelagic_switches", i, j)) {
				g_Vars.aioffset += 2;
				return 1;
			}
			buttonsdone[j] = 1;
		}
	}
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_shuffle_pelagic_switches_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action shuffle_pelagic_switches source=%s objects=%s backend=graph.ai.action.quip_shuffle+ai/ailists.tsv+objects.tsv",
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_shuffle_pelagic_switches_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryAttackAmount(struct chrdata *chr,
	s32 arg0, s32 arg1)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_try_attack_amount_node_count != 1) {
		return s_aiGraphRuntimeFailure("try_attack_amount",
			"missing scenario.ai.action.try_attack_amount node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("try_attack_amount", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("try_attack_amount",
			"missing ai/ailists.tsv source");
	}
	chrTryAttackAmount(chr, 512, 0, arg0, arg1);
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_try_attack_amount_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_attack_amount arg0=%d arg1=%d source=%s backend=graph.ai.action.orders+ai/ailists.tsv",
			arg0, arg1, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_try_attack_amount_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetMorale(struct chrdata *chr, s32 morale)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_morale_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_morale",
			"missing scenario.ai.action.set_morale node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_morale", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_morale",
			"missing ai/ailists.tsv source");
	}
	chr->morale = (u8)morale;
	if (!s_ActiveScenarioGraphs.ai_action_set_morale_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_morale value=%d source=%s backend=graph.ai.action.set_morale+ai/ailists.tsv",
			morale, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_morale_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAddMorale(struct chrdata *chr, s32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_add_morale_node_count != 1) {
		return s_aiGraphRuntimeFailure("add_morale",
			"missing scenario.ai.action.add_morale node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("add_morale", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("add_morale",
			"missing ai/ailists.tsv source");
	}
	incrementByte(&chr->morale, (u8)amount);
	if (!s_ActiveScenarioGraphs.ai_action_add_morale_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action add_morale amount=%d source=%s backend=graph.ai.action.add_morale+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_add_morale_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrAddMorale(struct chrdata *basechr,
	s32 amount, s32 chrnum)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_add_morale_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_add_morale",
			"missing scenario.ai.action.chr_add_morale node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_add_morale", "missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_add_morale",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (!chr) {
		return s_aiGraphRuntimeFailure("chr_add_morale",
			"missing target chr");
	}
	incrementByte(&chr->morale, (u8)amount);
	if (!s_ActiveScenarioGraphs.ai_action_chr_add_morale_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_add_morale chr=%d amount=%d source=%s backend=graph.ai.action.chr_add_morale+ai/ailists.tsv",
			chrnum, amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_add_morale_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSubtractMorale(struct chrdata *chr,
	s32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_subtract_morale_node_count != 1) {
		return s_aiGraphRuntimeFailure("subtract_morale",
			"missing scenario.ai.action.subtract_morale node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("subtract_morale", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("subtract_morale",
			"missing ai/ailists.tsv source");
	}
	decrementByte(&chr->morale, (u8)amount);
	if (!s_ActiveScenarioGraphs.ai_action_subtract_morale_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action subtract_morale amount=%d source=%s backend=graph.ai.action.subtract_morale+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_subtract_morale_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetAlertness(struct chrdata *chr,
	s32 alertness)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_alertness_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_alertness",
			"missing scenario.ai.action.set_alertness node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_alertness", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_alertness",
			"missing ai/ailists.tsv source");
	}
	chr->alertness = (u8)alertness;
	if (!s_ActiveScenarioGraphs.ai_action_set_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_alertness value=%d source=%s backend=graph.ai.action.set_alertness+ai/ailists.tsv",
			alertness, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAddAlertness(struct chrdata *chr,
	s32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_add_alertness_node_count != 1) {
		return s_aiGraphRuntimeFailure("add_alertness",
			"missing scenario.ai.action.add_alertness node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("add_alertness", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("add_alertness",
			"missing ai/ailists.tsv source");
	}
	incrementByte(&chr->alertness, (u8)amount);
	if (!s_ActiveScenarioGraphs.ai_action_add_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action add_alertness amount=%d source=%s backend=graph.ai.action.add_alertness+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_add_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrAddAlertness(struct chrdata *basechr,
	s32 amount, s32 chrnum)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_add_alertness_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_add_alertness",
			"missing scenario.ai.action.chr_add_alertness node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_add_alertness",
			"missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_add_alertness",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop) {
		incrementByte(&chr->alertness, (u8)amount);
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_add_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_add_alertness chr=%d amount=%d applied=%d source=%s backend=graph.ai.action.chr_add_alertness+ai/ailists.tsv",
			chrnum, amount, chr && chr->prop ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_add_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIncreaseSquadronAlertness(
	struct chrdata *chr, s32 amount)
{
	s16 *chrnums;
	s32 affected;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_increase_squadron_alertness_node_count != 1) {
		return s_aiGraphRuntimeFailure("increase_squadron_alertness",
			"missing scenario.ai.action.increase_squadron_alertness node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("increase_squadron_alertness",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("increase_squadron_alertness",
			"missing ai/ailists.tsv source");
	}

	affected = 0;
	chrnums = teamGetChrIds(chr->team);

	for (; *chrnums != -2; chrnums++) {
		struct chrdata *target = chrFindByLiteralId(*chrnums);

		if (target &&
				target->model &&
				!chrIsDead(target) &&
				target->actiontype != ACT_DEAD &&
				(chr->squadron == target->squadron || chr->squadron == 255) &&
				chr->chrnum != target->chrnum &&
				(chrGetDistanceToChr(chr, target->chrnum) < 1000 ||
					chrHasFlag(chr, CHRFLAG0_SQUADALERTANYDIST, BANK_0))) {
			incrementByte(&target->alertness, (u8)amount);
			affected++;
		}
	}

	if (!s_ActiveScenarioGraphs.ai_action_increase_squadron_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action increase_squadron_alertness amount=%d affected=%d source=%s backend=graph.ai.action.increase_squadron_alertness+ai/ailists.tsv",
			amount, affected, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_increase_squadron_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSubtractAlertness(struct chrdata *chr,
	s32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_subtract_alertness_node_count != 1) {
		return s_aiGraphRuntimeFailure("subtract_alertness",
			"missing scenario.ai.action.subtract_alertness node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("subtract_alertness", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("subtract_alertness",
			"missing ai/ailists.tsv source");
	}
	decrementByte(&chr->alertness, (u8)amount);
	if (!s_ActiveScenarioGraphs.ai_action_subtract_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action subtract_alertness amount=%d source=%s backend=graph.ai.action.subtract_alertness+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_subtract_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetHearDistance(struct chrdata *chr,
	f32 distance)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_hear_distance_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_hear_distance",
			"missing scenario.ai.action.set_hear_distance node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_hear_distance", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_hear_distance",
			"missing ai/ailists.tsv source");
	}
	chr->hearingscale = distance;
	if (!s_ActiveScenarioGraphs.ai_action_set_hear_distance_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_hear_distance value=%.3f source=%s backend=graph.ai.action.set_hear_distance+ai/ailists.tsv",
			distance, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_hear_distance_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetViewDistance(struct chrdata *chr,
	s32 distance)
{
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_view_distance_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_view_distance",
			"missing scenario.ai.action.set_view_distance node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_view_distance", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_view_distance",
			"missing ai/ailists.tsv source");
	}
	applied = 0;
	if (!cheatIsActive(CHEAT_PERFECTDARKNESS)) {
		chr->visionrange = (u8)distance;
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_view_distance_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_view_distance value=%d applied=%d source=%s backend=graph.ai.action.set_view_distance+ai/ailists.tsv",
			distance, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_view_distance_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetGrenadeProbability(struct chrdata *chr,
	s32 probability)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_grenade_probability_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_grenade_probability",
			"missing scenario.ai.action.set_grenade_probability node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_grenade_probability",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_grenade_probability",
			"missing ai/ailists.tsv source");
	}
	chr->grenadeprob = (u8)probability;
	if (!s_ActiveScenarioGraphs.ai_action_set_grenade_probability_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_grenade_probability value=%d source=%s backend=graph.ai.action.set_grenade_probability+ai/ailists.tsv",
			probability, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_grenade_probability_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrNum(struct chrdata *chr, s32 chrnum)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chr_num_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chr_num",
			"missing scenario.ai.action.set_chr_num node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_chr_num", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chr_num",
			"missing ai/ailists.tsv source");
	}
	chrSetChrnum(chr, chrnum);
	chr->chrnum = (s16)chrnum;
	if (!s_ActiveScenarioGraphs.ai_action_set_chr_num_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_num value=%d source=%s backend=graph.ai.action.set_chr_num+ai/ailists.tsv",
			chrnum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_num_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetMaxDamage(struct chrdata *basechr,
	struct chopperobj *hovercar, s32 chrnum, f32 maxdamage)
{
	struct chrdata *chr;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_max_damage_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_max_damage",
			"missing scenario.ai.action.set_max_damage node");
	}
	if (!basechr && !hovercar) {
		return s_aiGraphRuntimeFailure("set_max_damage",
			"missing base chr or hovercar");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_max_damage",
			"missing ai/ailists.tsv source");
	}
	applied = 0;
	if (hovercar) {
		chopperSetMaxDamage(hovercar, maxdamage);
		applied = 1;
	} else {
		chr = chrFindById(basechr, chrnum);
		if (chr && chr->prop && !chrIsDead(chr) &&
				chr->actiontype != ACT_DEAD &&
				chr->actiontype != ACT_DIE &&
				chr->actiontype != ACT_DRUGGEDKO &&
				chr->actiontype != ACT_DRUGGEDDROP &&
				chr->actiontype != ACT_DRUGGEDCOMINGUP) {
			chrSetMaxDamage(chr, maxdamage);
			applied = 1;
		}
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_max_damage_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_max_damage chr=%d value=%.3f applied=%d source=%s backend=graph.ai.action.set_max_damage+ai/ailists.tsv",
			chrnum, maxdamage, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_max_damage_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAddHealth(struct chrdata *chr, f32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_add_health_node_count != 1) {
		return s_aiGraphRuntimeFailure("add_health",
			"missing scenario.ai.action.add_health node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("add_health", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("add_health",
			"missing ai/ailists.tsv source");
	}
	chrAddHealth(chr, amount);
	if (!s_ActiveScenarioGraphs.ai_action_add_health_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action add_health amount=%.3f source=%s backend=graph.ai.action.add_health+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_add_health_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetShield(struct chrdata *chr, f32 amount)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_shield_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_shield",
			"missing scenario.ai.action.set_shield node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_shield", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_shield",
			"missing ai/ailists.tsv source");
	}
	if (cheatIsActive(CHEAT_ENEMYSHIELDS)) {
		amount = amount < 8 ? 8 : amount;
	}
	chrSetShield(chr, amount);
	if (!s_ActiveScenarioGraphs.ai_action_set_shield_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_shield value=%.3f source=%s backend=graph.ai.action.set_shield+ai/ailists.tsv",
			amount, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_shield_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetReactionSpeed(struct chrdata *chr,
	s32 speed)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_reaction_speed_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_reaction_speed",
			"missing scenario.ai.action.set_reaction_speed node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_reaction_speed", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_reaction_speed",
			"missing ai/ailists.tsv source");
	}
	chr->speedrating = (s8)speed;
	if (!s_ActiveScenarioGraphs.ai_action_set_reaction_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_reaction_speed value=%d source=%s backend=graph.ai.action.set_reaction_speed+ai/ailists.tsv",
			speed, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_reaction_speed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetRecoverySpeed(struct chrdata *chr,
	s32 speed)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_recovery_speed_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_recovery_speed",
			"missing scenario.ai.action.set_recovery_speed node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_recovery_speed", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_recovery_speed",
			"missing ai/ailists.tsv source");
	}
	chr->arghrating = (s8)speed;
	if (!s_ActiveScenarioGraphs.ai_action_set_recovery_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_recovery_speed value=%d source=%s backend=graph.ai.action.set_recovery_speed+ai/ailists.tsv",
			speed, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_recovery_speed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetAccuracy(struct chrdata *chr,
	s32 accuracy)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_accuracy_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_accuracy",
			"missing scenario.ai.action.set_accuracy node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_accuracy", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_accuracy",
			"missing ai/ailists.tsv source");
	}
	chr->accuracyrating = (s8)accuracy;
	if (!s_ActiveScenarioGraphs.ai_action_set_accuracy_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_accuracy value=%d source=%s backend=graph.ai.action.set_accuracy+ai/ailists.tsv",
			accuracy, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_accuracy_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetDodgeRating(struct chrdata *chr,
	s32 mode, s32 rating)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_dodge_rating_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_dodge_rating",
			"missing scenario.ai.action.set_dodge_rating node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_dodge_rating", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_dodge_rating",
			"missing ai/ailists.tsv source");
	}
	if (mode == 0) {
		chr->dodgerating = (s8)rating;
	} else if (mode == 1) {
		chr->maxdodgerating = (s8)rating;
	} else {
		chr->dodgerating = (s8)rating;
		chr->maxdodgerating = (s8)rating;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_dodge_rating_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_dodge_rating mode=%d value=%d source=%s backend=graph.ai.action.set_dodge_rating+ai/ailists.tsv",
			mode, rating, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_dodge_rating_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetUnarmedDodgeRating(struct chrdata *chr,
	s32 rating)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_unarmed_dodge_rating_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_unarmed_dodge_rating",
			"missing scenario.ai.action.set_unarmed_dodge_rating node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_unarmed_dodge_rating",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_unarmed_dodge_rating",
			"missing ai/ailists.tsv source");
	}
	chr->unarmeddodgerating = (s8)rating;
	if (!s_ActiveScenarioGraphs.ai_action_set_unarmed_dodge_rating_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_unarmed_dodge_rating value=%d source=%s backend=graph.ai.action.set_unarmed_dodge_rating+ai/ailists.tsv",
			rating, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_unarmed_dodge_rating_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetFlag(struct chrdata *chr,
	u32 flags, u8 bank)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_flag",
			"missing scenario.ai.action.set_flag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_flag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_flag",
			"missing ai/ailists.tsv source");
	}
	chrSetFlags(chr, flags, bank);
	if (!s_ActiveScenarioGraphs.ai_action_set_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_flag flags=0x%08x bank=%u source=%s backend=graph.ai.action.set_flag+ai/ailists.tsv",
			flags, (unsigned)bank,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnsetFlag(struct chrdata *chr,
	u32 flags, u8 bank)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_unset_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("unset_flag",
			"missing scenario.ai.action.unset_flag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("unset_flag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("unset_flag",
			"missing ai/ailists.tsv source");
	}
	chrUnsetFlags(chr, flags, bank);
	if (!s_ActiveScenarioGraphs.ai_action_unset_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unset_flag flags=0x%08x bank=%u source=%s backend=graph.ai.action.unset_flag+ai/ailists.tsv",
			flags, (unsigned)bank,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_unset_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHasFlag(struct chrdata *chr,
	u32 flags, u8 bank, s32 invert, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_has_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_has_flag",
			"missing scenario.ai.action.if_has_flag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("if_has_flag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_has_flag",
			"missing ai/ailists.tsv source");
	}
	result = chrHasFlag(chr, flags, bank) ? 1 : 0;
	if (invert) {
		result = !result;
	}
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 9;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_has_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_has_flag flags=0x%08x bank=%u invert=%d result=%d source=%s backend=graph.ai.action.if_has_flag+ai/ailists.tsv",
			flags, (unsigned)bank, invert, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_has_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_flag",
			"missing scenario.ai.action.chr_set_flag node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_set_flag",
			"missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_flag",
			"missing ai/ailists.tsv source");
	}
	chrSetFlagsById(basechr, chrnum, flags, bank);
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_flag chr=%d flags=0x%08x bank=%u source=%s backend=graph.ai.action.chr_set_flag+ai/ailists.tsv",
			chrnum, flags, (unsigned)bank,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrUnsetFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_unset_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_unset_flag",
			"missing scenario.ai.action.chr_unset_flag node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_unset_flag",
			"missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_unset_flag",
			"missing ai/ailists.tsv source");
	}
	chrUnsetFlagsById(basechr, chrnum, flags, bank);
	if (!s_ActiveScenarioGraphs.ai_action_chr_unset_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_unset_flag chr=%d flags=0x%08x bank=%u source=%s backend=graph.ai.action.chr_unset_flag+ai/ailists.tsv",
			chrnum, flags, (unsigned)bank,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_unset_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrHasFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_chr_has_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_chr_has_flag",
			"missing scenario.ai.action.if_chr_has_flag node");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("if_chr_has_flag",
			"missing base chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_chr_has_flag",
			"missing ai/ailists.tsv source");
	}
	result = chrHasFlagById(basechr, chrnum, flags, bank) ? 1 : 0;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 9;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_chr_has_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_chr_has_flag chr=%d flags=0x%08x bank=%u result=%d source=%s backend=graph.ai.action.if_chr_has_flag+ai/ailists.tsv",
			chrnum, flags, (unsigned)bank, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_chr_has_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetStageFlag(u32 flags)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_stage_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_stage_flag",
			"missing scenario.ai.action.set_stage_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_stage_flag",
			"missing ai/ailists.tsv source");
	}
	chrSetStageFlag(NULL, flags);
	if (!s_ActiveScenarioGraphs.ai_action_set_stage_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_stage_flag flags=0x%08x source=%s backend=graph.ai.action.set_stage_flag+ai/ailists.tsv",
			flags, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_stage_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnsetStageFlag(u32 flags)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_unset_stage_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("unset_stage_flag",
			"missing scenario.ai.action.unset_stage_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("unset_stage_flag",
			"missing ai/ailists.tsv source");
	}
	chrUnsetStageFlag(NULL, flags);
	if (!s_ActiveScenarioGraphs.ai_action_unset_stage_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unset_stage_flag flags=0x%08x source=%s backend=graph.ai.action.unset_stage_flag+ai/ailists.tsv",
			flags, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_unset_stage_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStageFlagEq(u32 flags, s32 expected,
	s32 label)
{
	s32 has_flag;
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_stage_flag_eq_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_stage_flag_eq",
			"missing scenario.ai.action.if_stage_flag_eq node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_stage_flag_eq",
			"missing ai/ailists.tsv source");
	}
	has_flag = chrHasStageFlag(NULL, flags) ? 1 : 0;
	result = (has_flag && expected == 1) || (!has_flag && expected == 0);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 8;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_stage_flag_eq_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_stage_flag_eq flags=0x%08x expected=%d result=%d source=%s backend=graph.ai.action.if_stage_flag_eq+ai/ailists.tsv",
			flags, expected, result,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_stage_flag_eq_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrflag(struct chrdata *chr, u32 flags)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chrflag",
			"missing scenario.ai.action.set_chrflag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("set_chrflag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chrflag",
			"missing ai/ailists.tsv source");
	}
	chr->chrflags |= flags;
	if (!s_ActiveScenarioGraphs.ai_action_set_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chrflag flags=0x%08x source=%s backend=graph.ai.action.set_chrflag+ai/ailists.tsv",
			flags, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chrflag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnsetChrflag(struct chrdata *chr, u32 flags)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_unset_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("unset_chrflag",
			"missing scenario.ai.action.unset_chrflag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("unset_chrflag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("unset_chrflag",
			"missing ai/ailists.tsv source");
	}
	chr->chrflags &= ~flags;
	if (!s_ActiveScenarioGraphs.ai_action_unset_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unset_chrflag flags=0x%08x source=%s backend=graph.ai.action.unset_chrflag+ai/ailists.tsv",
			flags, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_unset_chrflag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfHasChrflag(struct chrdata *chr,
	u32 flags, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_has_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_has_chrflag",
			"missing scenario.ai.action.if_has_chrflag node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("if_has_chrflag", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_has_chrflag",
			"missing ai/ailists.tsv source");
	}
	result = ((chr->chrflags & flags) == flags);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 7;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_has_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_has_chrflag flags=0x%08x result=%d source=%s backend=graph.ai.action.if_has_chrflag+ai/ailists.tsv",
			flags, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_has_chrflag_logged = 1;
	}
	return 1;
}

static struct chrdata *s_aiGraphFindChrById(struct chrdata *basechr,
	s32 chrnum, const char *action)
{
	if (!basechr) {
		s_aiGraphRuntimeFailure(action, "missing base chr");
		return NULL;
	}
	return chrFindById(basechr, chrnum);
}

s32 scenarioSourceAiGraphExecuteChrSetChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_chrflag",
			"missing scenario.ai.action.chr_set_chrflag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_chrflag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum, "chr_set_chrflag");
	if (chr) {
		chr->chrflags |= flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_chrflag chr=%d flags=0x%08x applied=%d source=%s backend=graph.ai.action.chr_set_chrflag+ai/ailists.tsv",
			chrnum, flags, chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_chrflag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrUnsetChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_unset_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_unset_chrflag",
			"missing scenario.ai.action.chr_unset_chrflag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_unset_chrflag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum, "chr_unset_chrflag");
	if (chr) {
		chr->chrflags &= ~flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_unset_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_unset_chrflag chr=%d flags=0x%08x applied=%d source=%s backend=graph.ai.action.chr_unset_chrflag+ai/ailists.tsv",
			chrnum, flags, chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_unset_chrflag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrHasChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags, s32 label)
{
	struct chrdata *chr;
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_chr_has_chrflag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_chr_has_chrflag",
			"missing scenario.ai.action.if_chr_has_chrflag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_chr_has_chrflag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum, "if_chr_has_chrflag");
	result = chr && (chr->chrflags & flags) == flags;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 8;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_chr_has_chrflag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_chr_has_chrflag chr=%d flags=0x%08x result=%d source=%s backend=graph.ai.action.if_chr_has_chrflag+ai/ailists.tsv",
			chrnum, flags, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_chr_has_chrflag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_hidden_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_hidden_flag",
			"missing scenario.ai.action.chr_set_hidden_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_hidden_flag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum, "chr_set_hidden_flag");
	if (chr) {
		chr->hidden |= flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_hidden_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_hidden_flag chr=%d flags=0x%08x applied=%d source=%s backend=graph.ai.action.chr_set_hidden_flag+ai/ailists.tsv",
			chrnum, flags, chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_hidden_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrUnsetHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_unset_hidden_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_unset_hidden_flag",
			"missing scenario.ai.action.chr_unset_hidden_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_unset_hidden_flag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum, "chr_unset_hidden_flag");
	if (chr) {
		chr->hidden &= ~flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_unset_hidden_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_unset_hidden_flag chr=%d flags=0x%08x applied=%d source=%s backend=graph.ai.action.chr_unset_hidden_flag+ai/ailists.tsv",
			chrnum, flags, chr ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_unset_hidden_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrHasHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, s32 label)
{
	struct chrdata *chr;
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_chr_has_hidden_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_chr_has_hidden_flag",
			"missing scenario.ai.action.if_chr_has_hidden_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_chr_has_hidden_flag",
			"missing ai/ailists.tsv source");
	}
	chr = s_aiGraphFindChrById(basechr, chrnum,
		"if_chr_has_hidden_flag");
	result = chr && (chr->hidden & flags) == flags;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 8;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_chr_has_hidden_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_chr_has_hidden_flag chr=%d flags=0x%08x result=%d source=%s backend=graph.ai.action.if_chr_has_hidden_flag+ai/ailists.tsv",
			chrnum, flags, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_chr_has_hidden_flag_logged = 1;
	}
	return 1;
}

static u32 *s_aiGraphObjectFlagBank(struct defaultobj *obj, s32 bank)
{
	if (!obj) {
		return NULL;
	}
	if (bank == 1) {
		return &obj->flags;
	}
	if (bank == 2) {
		return &obj->flags2;
	}
	if (bank == 3) {
		return &obj->flags3;
	}
	return NULL;
}

s32 scenarioSourceAiGraphExecuteSetObjFlag(s32 tag_id, u32 flags, s32 bank)
{
	struct defaultobj *obj;
	u32 *target;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_obj_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_obj_flag",
			"missing scenario.ai.action.set_obj_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("set_obj_flag",
			"missing ai/ailists.tsv or objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	target = s_aiGraphObjectFlagBank(obj, bank);
	if (target && obj->prop) {
		*target |= flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_obj_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_obj_flag tag=%d bank=%d flags=0x%08x applied=%d source=%s objects=%s backend=graph.ai.action.set_obj_flag+objects.tsv",
			tag_id, bank, flags, target && obj && obj->prop ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_set_obj_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnsetObjFlag(s32 tag_id, u32 flags, s32 bank)
{
	struct defaultobj *obj;
	u32 *target;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_unset_obj_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("unset_obj_flag",
			"missing scenario.ai.action.unset_obj_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("unset_obj_flag",
			"missing ai/ailists.tsv or objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	target = s_aiGraphObjectFlagBank(obj, bank);
	if (target && obj->prop) {
		*target &= ~flags;
	}
	if (!s_ActiveScenarioGraphs.ai_action_unset_obj_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unset_obj_flag tag=%d bank=%d flags=0x%08x applied=%d source=%s objects=%s backend=graph.ai.action.unset_obj_flag+objects.tsv",
			tag_id, bank, flags, target && obj && obj->prop ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_unset_obj_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjHasFlag(s32 tag_id, u32 flags, s32 bank,
	s32 label)
{
	struct defaultobj *obj;
	u32 *target;
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_obj_has_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_obj_has_flag",
			"missing scenario.ai.action.if_obj_has_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("if_obj_has_flag",
			"missing ai/ailists.tsv or objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	target = s_aiGraphObjectFlagBank(obj, bank);
	result = target && obj->prop && ((*target & flags) == flags);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 8;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_obj_has_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_obj_has_flag tag=%d bank=%d flags=0x%08x result=%d source=%s objects=%s backend=graph.ai.action.if_obj_has_flag+objects.tsv",
			tag_id, bank, flags, result,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_obj_has_flag_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireDoorNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv or objects.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteOpenDoor(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireDoorNode("open_door",
			s_ActiveScenarioGraphs.level_ai_open_door_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->type == PROPTYPE_DOOR) {
		if (!doorCallLift(obj->prop, false)) {
			doorsRequestMode((struct doorobj *)obj, DOORMODE_OPENING);
		}
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_open_door_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action open_door tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_open_door_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteCloseDoor(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireDoorNode("close_door",
			s_ActiveScenarioGraphs.level_ai_close_door_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->type == PROPTYPE_DOOR) {
		doorsRequestMode((struct doorobj *)obj, DOORMODE_CLOSING);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_close_door_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action close_door tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_close_door_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDoorState(s32 tag_id, u32 states, s32 label)
{
	struct defaultobj *obj;
	struct doorobj *door;
	s32 result = 0;

	if (!s_aiGraphRequireDoorNode("if_door_state",
			s_ActiveScenarioGraphs.level_ai_if_door_state_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->type == OBJTYPE_DOOR) {
		door = (struct doorobj *)obj;
		if (door->mode == DOORMODE_IDLE) {
			if (door->frac <= 0) {
				result = (states & DOORSTATE_CLOSED) != 0;
			} else {
				result = (states & DOORSTATE_OPEN) != 0;
			}
		} else if (door->mode == DOORMODE_OPENING ||
				door->mode == DOORMODE_WAITING) {
			result = (states & DOORSTATE_OPENING) != 0;
		} else if (door->mode == DOORMODE_CLOSING) {
			result = (states & DOORSTATE_CLOSING) != 0;
		}
	}
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_door_state_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_door_state tag=%d states=0x%02x result=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, (unsigned)states, result,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_door_state_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjectIsDoor(s32 tag_id, s32 label)
{
	struct defaultobj *obj;
	s32 result;

	if (!s_aiGraphRequireDoorNode("if_object_is_door",
			s_ActiveScenarioGraphs.level_ai_if_object_is_door_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	result = obj && obj->prop && obj->type == OBJTYPE_DOOR &&
		(obj->hidden & 0x200);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_object_is_door_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_object_is_door tag=%d result=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, result, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_object_is_door_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteLockDoor(s32 tag_id, u32 bits)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireDoorNode("lock_door",
			s_ActiveScenarioGraphs.level_ai_lock_door_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->type == PROPTYPE_DOOR) {
		((struct doorobj *)obj)->keyflags |= (u8)bits;
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_lock_door_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action lock_door tag=%d bits=0x%02x applied=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, (unsigned)bits, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_lock_door_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnlockDoor(s32 tag_id, u32 bits)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireDoorNode("unlock_door",
			s_ActiveScenarioGraphs.level_ai_unlock_door_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->type == PROPTYPE_DOOR) {
		((struct doorobj *)obj)->keyflags &= (u8)~bits;
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_unlock_door_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unlock_door tag=%d bits=0x%02x applied=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, (unsigned)bits, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_unlock_door_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDoorLocked(s32 tag_id, u32 bits, s32 label)
{
	struct defaultobj *obj;
	s32 result = 0;

	if (!s_aiGraphRequireDoorNode("if_door_locked",
			s_ActiveScenarioGraphs.level_ai_if_door_locked_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->prop->type == PROPTYPE_DOOR) {
		result = ((((struct doorobj *)obj)->keyflags & bits) == bits);
	}
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_door_locked_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_door_locked tag=%d bits=0x%02x result=%d source=%s objects=%s backend=graph.ai.action.door+objects.tsv",
			tag_id, (unsigned)bits, result,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_door_locked_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireLiftNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0] ||
			!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv, objects.tsv, or pads.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfLiftStationary(s32 tag_id, s32 label)
{
	struct defaultobj *obj;
	s32 result = 0;

	if (!s_aiGraphRequireLiftNode("if_lift_stationary",
			s_ActiveScenarioGraphs.level_ai_if_lift_stationary_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->type == OBJTYPE_LIFT) {
		struct liftobj *lift = (struct liftobj *)obj;
		result = (obj->flags & OBJFLAG_DEACTIVATED) || lift->dist == 0;
	}
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_lift_stationary_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_lift_stationary tag=%d result=%d source=%s objects=%s pads=%s backend=graph.ai.action.lift+objects.tsv+pads.tsv",
			tag_id, result, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_if_lift_stationary_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteLiftGoToStop(s32 tag_id, s32 stopnum)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireLiftNode("lift_go_to_stop",
			s_ActiveScenarioGraphs.level_ai_lift_go_to_stop_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->type == OBJTYPE_LIFT) {
		liftGoToStop((struct liftobj *)obj, stopnum);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_lift_go_to_stop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action lift_go_to_stop tag=%d stop=%d applied=%d source=%s objects=%s pads=%s backend=graph.ai.action.lift+objects.tsv+pads.tsv",
			tag_id, stopnum, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_lift_go_to_stop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfLiftAtStop(s32 tag_id, s32 stopnum,
	s32 label)
{
	struct defaultobj *obj;
	s32 result = 0;

	if (!s_aiGraphRequireLiftNode("if_lift_at_stop",
			s_ActiveScenarioGraphs.level_ai_if_lift_at_stop_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->type == OBJTYPE_LIFT) {
		struct liftobj *lift = (struct liftobj *)obj;
		result = lift->levelcur == stopnum && lift->dist == 0;
	}
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_lift_at_stop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_lift_at_stop tag=%d stop=%d result=%d source=%s objects=%s pads=%s backend=graph.ai.action.lift+objects.tsv+pads.tsv",
			tag_id, stopnum, result, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_if_lift_at_stop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteActivateLift(s32 liftnum, s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiGraphRequireLiftNode("activate_lift",
			s_ActiveScenarioGraphs.level_ai_activate_lift_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		liftActivate(obj->prop, (u8)liftnum);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_activate_lift_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action activate_lift liftnum=%d tag=%d applied=%d source=%s objects=%s pads=%s backend=graph.ai.action.lift+objects.tsv+pads.tsv",
			liftnum, tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_activate_lift_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfUsingLift(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_aiGraphRequireLiftNode("if_using_lift",
			s_ActiveScenarioGraphs.level_ai_if_using_lift_node_count)) {
		return 0;
	}
	result = chr && chrIsUsingLift(chr);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_using_lift_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_using_lift result=%d source=%s objects=%s pads=%s backend=graph.ai.action.lift+objects.tsv+pads.tsv",
			result, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_if_using_lift_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireWeatherNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.level_global_settings_node_count) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv or scenario.ini source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteConfigureRain(u32 intensity)
{
	if (!s_aiGraphRequireWeatherNode("configure_rain",
			s_ActiveScenarioGraphs.level_ai_configure_rain_node_count)) {
		return 0;
	}
	weatherConfigureRain(intensity);
	if (!s_ActiveScenarioGraphs.ai_action_configure_rain_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action configure_rain intensity=%u source=%s scenario=%s backend=graph.ai.action.weather+ai/ailists.tsv+scenario.ini",
			(unsigned)intensity, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.level_global_settings_scenario);
		s_ActiveScenarioGraphs.ai_action_configure_rain_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteConfigureSnow(u32 intensity)
{
	if (!s_aiGraphRequireWeatherNode("configure_snow",
			s_ActiveScenarioGraphs.level_ai_configure_snow_node_count)) {
		return 0;
	}
	weatherConfigureSnow(intensity);
	if (!s_ActiveScenarioGraphs.ai_action_configure_snow_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action configure_snow intensity=%u source=%s scenario=%s backend=graph.ai.action.weather+ai/ailists.tsv+scenario.ini",
			(unsigned)intensity, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.level_global_settings_scenario);
		s_ActiveScenarioGraphs.ai_action_configure_snow_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireSkyNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSwitchToAltSky(void)
{
	if (!s_aiGraphRequireSkyNode("switch_to_alt_sky",
			s_ActiveScenarioGraphs.level_ai_switch_to_alt_sky_node_count)) {
		return 0;
	}
	envApplyTransitionFrac(1);
	if (!s_ActiveScenarioGraphs.ai_action_switch_to_alt_sky_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action switch_to_alt_sky source=%s backend=graph.ai.action.sky+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_switch_to_alt_sky_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetWindSpeed(s32 speed)
{
	if (!s_aiGraphRequireSkyNode("set_wind_speed",
			s_ActiveScenarioGraphs.level_ai_set_wind_speed_node_count)) {
		return 0;
	}
	g_SkyWindSpeed = 0.1f * speed;
	if (!s_ActiveScenarioGraphs.ai_action_set_wind_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_wind_speed speed=%d value=%.3f source=%s backend=graph.ai.action.sky+ai/ailists.tsv",
			speed, g_SkyWindSpeed, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_wind_speed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetLights(struct chrdata *chr, s32 padnum,
	s32 operation, s32 arg1, s32 arg2, s32 duration)
{
	s32 roomnum;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_lights_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_lights",
			"missing scenario.ai.action.set_lights node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("set_lights",
			"missing ai/ailists.tsv or pads.tsv source");
	}
	roomnum = chrGetPadRoom(chr, padnum);
	if (roomnum >= 0) {
		switch (operation) {
		case LIGHTOP_TURNOFF:
			roomSetLightsOn(roomnum, false);
			break;
		case LIGHTOP_TURNON:
			roomSetLightsOn(roomnum, true);
			break;
		default:
			roomSetLightOp(roomnum, operation, arg1, arg2,
				TICKS(duration));
			break;
		}
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_lights_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_lights pad=%d room=%d op=%d source=%s pads=%s backend=graph.ai.action.lighting+pads.tsv",
			padnum, roomnum, operation, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_set_lights_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetRoomFlag(s32 roomnum, s32 flag)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_room_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_room_flag",
			"missing scenario.ai.action.set_room_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_room_flag",
			"missing ai/ailists.tsv source");
	}
	g_Rooms[roomnum].flags |= flag;
	if (!s_ActiveScenarioGraphs.ai_action_set_room_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_room_flag room=%d flag=0x%04x source=%s scene=scene.glb backend=graph.ai.action.room_flags+ai/ailists.tsv+scene.glb",
			roomnum, (u32)(flag & 0xffff), s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_room_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteShowCutsceneChrs(s32 show)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_show_cutscene_chrs_node_count != 1) {
		return s_aiGraphRuntimeFailure("show_cutscene_chrs",
			"missing scenario.ai.action.show_cutscene_chrs node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("show_cutscene_chrs",
			"missing ai/ailists.tsv source");
	}
	if (show) {
		for (i = chrsGetNumSlots() - 1; i >= 0; i--) {
			if (g_ChrSlots[i].chrnum >= 0 && g_ChrSlots[i].prop &&
					(g_ChrSlots[i].hidden2 & CHRH2FLAG_HIDDENFORCUTSCENE)) {
				g_ChrSlots[i].hidden2 &= ~CHRH2FLAG_HIDDENFORCUTSCENE;
				g_ChrSlots[i].chrflags &= ~CHRCFLAG_HIDDEN;
			}
		}
	} else {
		for (i = chrsGetNumSlots() - 1; i >= 0; i--) {
			if (g_ChrSlots[i].chrnum >= 0 && g_ChrSlots[i].prop &&
					(g_ChrSlots[i].chrflags &
						(CHRCFLAG_UNPLAYABLE | CHRCFLAG_HIDDEN)) == 0) {
				g_ChrSlots[i].hidden2 |= CHRH2FLAG_HIDDENFORCUTSCENE;
				g_ChrSlots[i].chrflags |= CHRCFLAG_HIDDEN;
			}
		}
	}
	if (!s_ActiveScenarioGraphs.ai_action_show_cutscene_chrs_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action show_cutscene_chrs show=%d source=%s backend=graph.ai.action.cutscene_visibility+ai/ailists.tsv",
			show ? 1 : 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_show_cutscene_chrs_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteConfigureEnvironment(s32 roomnum,
	s32 command, s32 value)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_configure_environment_node_count != 1) {
		return s_aiGraphRuntimeFailure("configure_environment",
			"missing scenario.ai.action.configure_environment node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("configure_environment",
			"missing ai/ailists.tsv source");
	}

	switch (command) {
	case AIENVCMD_00:
		var8006ae18 = value;
		break;
	case AIENVCMD_01:
		var8006ae1c = value;
		break;
	case AIENVCMD_02:
		var8006ae20 = value;
		break;
	case AIENVCMD_03:
		var8006ae24 = value;
		break;
	case AIENVCMD_04:
		var8006ae28 = value;
		break;
	case AIENVCMD_ROOM_SETAMBIENT:
		g_Rooms[roomnum].flags &= ~ROOMFLAG_PLAYAMBIENTTRACK;
		if (value) {
			g_Rooms[roomnum].flags |= ROOMFLAG_PLAYAMBIENTTRACK;
		}
		break;
	case AIENVCMD_ROOM_SETOUTDOORS:
		g_Rooms[roomnum].flags &= ~ROOMFLAG_OUTDOORS;
		if (value) {
			g_Rooms[roomnum].flags |= ROOMFLAG_OUTDOORS;
		}
		break;
	case AIENVCMD_07:
		g_Rooms[roomnum].unk4e_04 = value;
		break;
	case AIENVCMD_08:
		g_Rooms[roomnum].unk4d = value;
		break;
	case AIENVCMD_SETAMBIENT:
		for (i = 1; i < g_Vars.roomcount; i++) {
			if (value) {
				g_Rooms[i].flags |= ROOMFLAG_PLAYAMBIENTTRACK;
			} else {
				g_Rooms[i].flags &= ~ROOMFLAG_PLAYAMBIENTTRACK;
			}
		}
		break;
	case AIENVCMD_PLAYNOSEDIVE:
		sndPlayNosedive(value);
		break;
	case AIENVCMD_TICKMUSICQUEUE:
		musicTickEvents();
		break;
	case AIENVCMD_ROOM_SETFAULTYLIGHTS:
		roomSetLightsFaulty(roomnum, value);
		break;
	case AIENVCMD_STOPNOSEDIVE:
		sndStopNosedive();
		break;
	case AIENVCMD_PLAYUFOHUM:
		sndPlayUfo(value);
		break;
	case AIENVCMD_STOPUFOHUM:
		sndStopUfo();
		break;
	}

	if (!s_ActiveScenarioGraphs.ai_action_configure_environment_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action configure_environment room=%d command=0x%02x value=%d source=%s backend=graph.ai.action.environment+ai/ailists.tsv+scenario.ini+scene.glb",
			roomnum, (u32)(command & 0xff), value,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_configure_environment_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDistanceToTarget2(struct chrdata *chr,
	f32 distance, s32 greater_than, s32 label)
{
	f32 actual;
	s32 matched;
	const char *action;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}

	action = greater_than
		? "if_distance_to_target2_greater_than"
		: "if_distance_to_target2_less_than";

	if (greater_than) {
		if (s_ActiveScenarioGraphs
				.level_ai_if_distance_to_target2_greater_than_node_count != 1) {
			return s_aiGraphRuntimeFailure(action,
				"missing scenario.ai.condition.if_distance_to_target2_greater_than node");
		}
	} else if (s_ActiveScenarioGraphs
			.level_ai_if_distance_to_target2_less_than_node_count != 1) {
		return s_aiGraphRuntimeFailure(action,
			"missing scenario.ai.condition.if_distance_to_target2_less_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure(action, "missing chr");
	}

	actual = chrGetDistanceToTarget2(chr);
	matched = greater_than ? actual > distance : actual < distance;

	if (matched) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}

	if (!s_ActiveScenarioGraphs.ai_action_if_distance_to_target2_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition %s actual=%.2f threshold=%.2f label=%d source=%s backend=graph.ai.condition.target_distance+ai/ailists.tsv",
			action, (double)actual, (double)distance, label,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_distance_to_target2_logged = 1;
	}
	return 1;
}

static s32 s_aiAudioGraphReady(const char *action, s32 node_count,
	const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSpeak(struct chrdata *basechr, s32 chrnum,
	s16 text_id, s16 audio_id, s8 channel, s32 subtitle_timer)
{
	struct chrdata *chr;
	s32 prevplayernum;
	s32 playernum;
	u32 channelnum;
	char *text;
	s32 ready = s_aiAudioGraphReady("speak",
		s_ActiveScenarioGraphs.level_ai_speak_node_count,
		"missing scenario.ai.action.speak node");

	if (ready != 1) {
		return ready;
	}
	chr = chrFindById(basechr, chrnum);
	prevplayernum = g_Vars.currentplayernum;
	playernum = prevplayernum;
	text = text_id >= 0 ? langGet(text_id) : NULL;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		playernum = playermgrGetPlayerNumByProp(chr->prop);
	}
	setCurrentPlayerNum(playernum);
	if (text && chrnum != CHR_P1P2) {
		psStopSound(basechr->prop, PSTYPE_CHRTALK, 0xffff);
	}
	if (chrnum == CHR_P1P2) {
		channelnum = psPlayFromProp(channel, audio_id, 0, basechr->prop,
			PSTYPE_NONE, PSFLAG_FORHUDMSG);
	} else {
		channelnum = psPlayFromProp(channel, audio_id, 0, basechr->prop,
			PSTYPE_CHRTALK, PSFLAG_FORHUDMSG);
	}
	if (text && !sndIsFiltered(audio_id)) {
		hudmsgCreateAsSubtitle(text, HUDMSGTYPE_INGAMESUBTITLE,
			subtitle_timer, channelnum);
	}
	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 9;
	if (!s_ActiveScenarioGraphs.ai_action_speak_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action speak chr=%d text=%d audio=%d channel=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			chrnum, text_id, audio_id, channel,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_speak_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlaySound(s8 channel, s16 audio_id)
{
	s32 ready = s_aiAudioGraphReady("play_sound",
		s_ActiveScenarioGraphs.level_ai_play_sound_node_count,
		"missing scenario.ai.action.play_sound node");
	if (ready != 1) {
		return ready;
	}
	psPlayFromProp(channel, audio_id, 0, NULL, PSTYPE_NONE, 0);
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_play_sound_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_sound channel=%d audio=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, audio_id, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_sound_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAssignSound(s8 channel, s16 audio_id)
{
	s32 ready = s_aiAudioGraphReady("assign_sound",
		s_ActiveScenarioGraphs.level_ai_assign_sound_node_count,
		"missing scenario.ai.action.assign_sound node");
	if (ready != 1) {
		return ready;
	}
	psPlayFromProp(channel, audio_id, -1, NULL, PSTYPE_MARKER, 0);
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_assign_sound_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action assign_sound channel=%d audio=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, audio_id, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_assign_sound_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteAudioMuteChannel(s8 channel)
{
	s32 ready = s_aiAudioGraphReady("audio_mute_channel",
		s_ActiveScenarioGraphs.level_ai_audio_mute_channel_node_count,
		"missing scenario.ai.action.audio_mute_channel node");
	if (ready != 1) {
		return ready;
	}
	psMuteChannel(channel);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_audio_mute_channel_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action audio_mute_channel channel=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_audio_mute_channel_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChannelFree(s8 channel, s32 label)
{
	s32 free_channel;
	s32 ready = s_aiAudioGraphReady("if_channel_free",
		s_ActiveScenarioGraphs.level_ai_if_channel_free_node_count,
		"missing scenario.ai.condition.if_channel_free node");
	if (ready != 1) {
		return ready;
	}
	free_channel = psIsChannelFree(channel);
	if (free_channel) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_channel_free_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_channel_free channel=%d label=%d branch=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, label, free_channel,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_channel_free_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetObjectSoundVolume(s8 channel, s16 volume,
	u16 volchangetimer60)
{
	s32 ready = s_aiAudioGraphReady("set_object_sound_volume",
		s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_node_count,
		"missing scenario.ai.action.set_object_sound_volume node");
	if (ready != 1) {
		return ready;
	}
	psModify(channel, volume, -1, NULL, volchangetimer60, 2500, 3000, 0);
	g_Vars.aioffset += 7;
	if (!s_ActiveScenarioGraphs.ai_action_set_object_sound_volume_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_object_sound_volume channel=%d volume=%d timer=%u source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, volume, volchangetimer60,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_object_sound_volume_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetObjectSoundVolumeByDistance(s8 channel,
	f32 playerdist, u16 volchangetimer60)
{
	s32 volume;
	s32 ready = s_aiAudioGraphReady("set_object_sound_volume_by_distance",
		s_ActiveScenarioGraphs.level_ai_set_object_sound_volume_by_distance_node_count,
		"missing scenario.ai.action.set_object_sound_volume_by_distance node");
	if (ready != 1) {
		return ready;
	}
	volume = psCalculateVolumeFromDistance(playerdist, 400, 2500, 3000,
		AL_VOL_FULL);
	psModify(channel, volume, -1, NULL, volchangetimer60, 2500, 3000, 0);
	g_Vars.aioffset += 7;
	if (!s_ActiveScenarioGraphs
			.ai_action_set_object_sound_volume_by_distance_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_object_sound_volume_by_distance channel=%d distance=%.2f volume=%d timer=%u source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, (double)playerdist, volume, volchangetimer60,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_set_object_sound_volume_by_distance_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetObjectSoundPlaying(s8 channel, s32 tag_id,
	u16 volchangetimer60)
{
	struct defaultobj *obj;
	s32 applied;
	s32 ready = s_aiAudioGraphReady("set_object_sound_playing",
		s_ActiveScenarioGraphs.level_ai_set_object_sound_playing_node_count,
		"missing scenario.ai.action.set_object_sound_playing node");
	if (ready != 1) {
		return ready;
	}
	obj = objFindByTagId(tag_id);
	applied = obj && obj->prop;
	if (applied) {
		psModify(channel, -1, -1, obj->prop, volchangetimer60,
			2500, 3000, 0);
	}
	g_Vars.aioffset += 6;
	if (!s_ActiveScenarioGraphs.ai_action_set_object_sound_playing_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_object_sound_playing tag=%d channel=%d timer=%u applied=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			tag_id, channel, volchangetimer60, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_object_sound_playing_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayRepeatingSoundFromObject(s8 channel,
	s32 tag_id, u16 volchangetimer60, u16 dist2, u16 dist3)
{
	struct defaultobj *obj;
	s32 applied;
	s32 timer = volchangetimer60 == 0 ? -1 : volchangetimer60;
	s32 ready = s_aiAudioGraphReady("play_repeating_sound_from_object",
		s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_object_node_count,
		"missing scenario.ai.action.play_repeating_sound_from_object node");
	if (ready != 1) {
		return ready;
	}
	obj = objFindByTagId(tag_id);
	applied = obj && obj->prop;
	if (applied) {
		psModify(channel, -1, -1, obj->prop, timer, dist2, dist3,
			PSFLAG_REPEATING);
	}
	g_Vars.aioffset += 10;
	if (!s_ActiveScenarioGraphs
			.ai_action_play_repeating_sound_from_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_repeating_sound_from_object tag=%d channel=%d timer=%d dist2=%u dist3=%u applied=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			tag_id, channel, timer, dist2, dist3, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_play_repeating_sound_from_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlaySoundFromEntity(s8 channel, s32 entity_id,
	u16 volchangetimer60, u16 dist2, u16 dist3, s32 entity_is_chr)
{
	struct prop *prop = NULL;
	s32 ready = s_aiAudioGraphReady("play_sound_from_entity",
		s_ActiveScenarioGraphs.level_ai_play_sound_from_entity_node_count,
		"missing scenario.ai.action.play_sound_from_entity node");
	if (ready != 1) {
		return ready;
	}
	if (entity_is_chr == 0) {
		struct defaultobj *obj = objFindByTagId(entity_id);
		if (obj) {
			prop = obj->prop;
		}
	} else {
		struct chrdata *chr = chrFindById(g_Vars.chrdata, entity_id);
		if (chr) {
			prop = chr->prop;
		}
	}
	if (prop) {
		psModify(channel, -1, -1, prop, volchangetimer60,
			dist2, dist3, 0);
	}
	g_Vars.aioffset += 11;
	if (!s_ActiveScenarioGraphs.ai_action_play_sound_from_entity_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_sound_from_entity entity=%d is_chr=%d channel=%d timer=%u dist2=%u dist3=%u applied=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			entity_id, entity_is_chr, channel, volchangetimer60,
			dist2, dist3, prop != NULL, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_sound_from_entity_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad(s16 padnum,
	s16 sound)
{
	s32 ready = s_aiAudioGraphReady("play_repeating_sound_from_pad",
		s_ActiveScenarioGraphs.level_ai_play_repeating_sound_from_pad_node_count,
		"missing scenario.ai.action.play_repeating_sound_from_pad node");
	if (ready != 1) {
		return ready;
	}
	psCreate(0, NULL, sound, padnum, -1, PSFLAG_REPEATING, 0,
		PSTYPE_NONE, 0, -1, 0, -1, -1, -1, -1);
	g_Vars.aioffset += 7;
	if (!s_ActiveScenarioGraphs.ai_action_play_repeating_sound_from_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_repeating_sound_from_pad pad=%d sound=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			padnum, sound, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_repeating_sound_from_pad_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjectSoundVolumeLessThan(s8 channel,
	s16 value, s32 label)
{
	s32 current;
	s32 branch;
	s32 ready = s_aiAudioGraphReady("if_object_sound_volume_less_than",
		s_ActiveScenarioGraphs.level_ai_if_object_sound_volume_less_than_node_count,
		"missing scenario.ai.condition.if_object_sound_volume_less_than node");
	if (ready != 1) {
		return ready;
	}
	current = psGetVolume(channel);
	branch = current < value;
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 6;
	}
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_object_sound_volume_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_object_sound_volume_less_than channel=%d value=%d current=%d label=%d branch=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			channel, value, current, label, branch,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_object_sound_volume_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlaySoundFromProp(s32 channel, s16 audio_id,
	s32 volume, s32 tag_id, s16 type, u16 flags)
{
	struct defaultobj *obj;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_play_sound_from_prop_node_count != 1) {
		return s_aiGraphRuntimeFailure("play_sound_from_prop",
			"missing scenario.ai.action.play_sound_from_prop node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("play_sound_from_prop",
			"missing ai/ailists.tsv source");
	}
	obj = objFindByTagId(tag_id);
	applied = obj && obj->prop;
	if (applied) {
		psPlayFromProp(channel, audio_id, volume, obj->prop, type, flags);
	}
	if (!s_ActiveScenarioGraphs.ai_action_play_sound_from_prop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_sound_from_prop tag=%d channel=%d audio=%d volume=%d type=%d flags=0x%04x applied=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			tag_id, channel, audio_id, volume, type, flags, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_sound_from_prop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack(s32 tracknum)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_play_temporary_primary_track_node_count != 1) {
		return s_aiGraphRuntimeFailure("play_temporary_primary_track",
			"missing scenario.ai.action.play_temporary_primary_track node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("play_temporary_primary_track",
			"missing ai/ailists.tsv source");
	}
	musicStartTemporaryPrimary(tracknum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_play_temporary_primary_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_temporary_primary_track track=%d source=%s backend=graph.ai.action.audio+ai/ailists.tsv",
			tracknum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_temporary_primary_track_logged = 1;
	}
	return 1;
}

static s32 s_aiMusicTrackGraphReady(const char *action, s32 node_count,
	const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayXTrack(s32 reason, s32 tracknum,
	s32 volume)
{
	s32 ready = s_aiMusicTrackGraphReady("play_x_track",
		s_ActiveScenarioGraphs.level_ai_play_x_track_node_count,
		"missing scenario.ai.action.play_x_track node");

	if (ready != 1) {
		return ready;
	}
	musicSetXReason((s8)reason, tracknum, volume);
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_play_x_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_x_track reason=%d track=%d volume=%d source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			reason, tracknum, volume, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_x_track_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStopXTrack(s32 reason)
{
	s32 ready = s_aiMusicTrackGraphReady("stop_x_track",
		s_ActiveScenarioGraphs.level_ai_stop_x_track_node_count,
		"missing scenario.ai.action.stop_x_track node");

	if (ready != 1) {
		return ready;
	}
	musicUnsetXReason((s8)reason);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_stop_x_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action stop_x_track reason=%d source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			reason, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_stop_x_track_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayTrackIsolated(s32 tracknum)
{
	s32 ready = s_aiMusicTrackGraphReady("play_track_isolated",
		s_ActiveScenarioGraphs.level_ai_play_track_isolated_node_count,
		"missing scenario.ai.action.play_track_isolated node");

	if (ready != 1) {
		return ready;
	}
	if (tracknum == MUSIC_CI_TRAINING) {
		u16 volume = optionsGetMusicVolume();
		musicPlayTrackIsolated(tracknum);
		optionsSetMusicVolume(volume);
	} else {
		musicPlayTrackIsolated(tracknum);
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_play_track_isolated_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_track_isolated track=%d source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			tracknum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_track_isolated_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayDefaultTracks(void)
{
	s32 ready = s_aiMusicTrackGraphReady("play_default_tracks",
		s_ActiveScenarioGraphs.level_ai_play_default_tracks_node_count,
		"missing scenario.ai.action.play_default_tracks node");

	if (ready != 1) {
		return ready;
	}
	musicPlayDefaultTracks();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_play_default_tracks_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_default_tracks source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_default_tracks_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayCutsceneTrack(s32 tracknum)
{
	s32 ready = s_aiMusicTrackGraphReady("play_cutscene_track",
		s_ActiveScenarioGraphs.level_ai_play_cutscene_track_node_count,
		"missing scenario.ai.action.play_cutscene_track node");

	if (ready != 1) {
		return ready;
	}
	musicStartCutscene(tracknum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_play_cutscene_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_cutscene_track track=%d source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			tracknum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_cutscene_track_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStopCutsceneTrack(void)
{
	s32 ready = s_aiMusicTrackGraphReady("stop_cutscene_track",
		s_ActiveScenarioGraphs.level_ai_stop_cutscene_track_node_count,
		"missing scenario.ai.action.stop_cutscene_track node");

	if (ready != 1) {
		return ready;
	}
	musicEndCutscene();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_stop_cutscene_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action stop_cutscene_track source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_stop_cutscene_track_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayTemporaryTrack(s32 tracknum)
{
	s32 ready = s_aiMusicTrackGraphReady("play_temporary_track",
		s_ActiveScenarioGraphs.level_ai_play_temporary_track_node_count,
		"missing scenario.ai.action.play_temporary_track node");

	if (ready != 1) {
		return ready;
	}
	musicStartTemporaryAmbient(tracknum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_play_temporary_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action play_temporary_track track=%d source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			tracknum, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_play_temporary_track_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStopAmbientTrack(void)
{
	s32 ready = s_aiMusicTrackGraphReady("stop_ambient_track",
		s_ActiveScenarioGraphs.level_ai_stop_ambient_track_node_count,
		"missing scenario.ai.action.stop_ambient_track node");

	if (ready != 1) {
		return ready;
	}
	musicEndTemporaryAmbient();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_stop_ambient_track_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action stop_ambient_track source=%s backend=graph.ai.action.music_track+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_stop_ambient_track_logged = 1;
	}
	return 1;
}

static s32 s_aiPlayerWeaponStateGraphReady(const char *action,
	s32 node_count, const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static struct chrdata *s_aiFindPlayerChr(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr = chrFindById(basechr, chrnum);

	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		return chr;
	}
	return NULL;
}

s32 scenarioSourceAiGraphExecuteChrDrawWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerWeaponStateGraphReady("chr_draw_weapon",
			s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_node_count,
			"missing scenario.ai.action.chr_draw_weapon node")) {
		return 0;
	}
	chr = s_aiFindPlayerChr(basechr, chrnum);
	if (chr) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		bgunEquipWeapon2(0, (s8)weaponnum);
		bgunEquipWeapon2(1, 0);
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_draw_weapon_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_draw_weapon chr=%d weapon=%d applied=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, weaponnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_draw_weapon_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene(
	struct chrdata *basechr, s32 chrnum, s32 weaponnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerWeaponStateGraphReady("chr_draw_weapon_in_cutscene",
			s_ActiveScenarioGraphs.level_ai_chr_draw_weapon_in_cutscene_node_count,
			"missing scenario.ai.action.chr_draw_weapon_in_cutscene node")) {
		return 0;
	}
	chr = s_aiFindPlayerChr(basechr, chrnum);
	if (chr) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		bgunEquipWeapon((s8)weaponnum);
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_draw_weapon_in_cutscene_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_draw_weapon_in_cutscene chr=%d weapon=%d applied=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, weaponnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_draw_weapon_in_cutscene_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPlayerForceSpeed(struct chrdata *basechr,
	s32 chrnum, s32 speed_x, s32 speed_z)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerWeaponStateGraphReady("set_player_force_speed",
			s_ActiveScenarioGraphs.level_ai_set_player_force_speed_node_count,
			"missing scenario.ai.action.set_player_force_speed node")) {
		return 0;
	}
	chr = s_aiFindPlayerChr(basechr, chrnum);
	if (chr) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		g_Vars.currentplayer->bondforcespeed.x = (s8)speed_x;
		g_Vars.currentplayer->bondforcespeed.y = 0;
		g_Vars.currentplayer->bondforcespeed.z = (s8)speed_z;
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_set_player_force_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_player_force_speed chr=%d x=%d z=%d applied=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, speed_x, speed_z, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_player_force_speed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetInvincible(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerWeaponStateGraphReady("chr_set_invincible",
			s_ActiveScenarioGraphs.level_ai_chr_set_invincible_node_count,
			"missing scenario.ai.action.chr_set_invincible node")) {
		return 0;
	}
	chr = s_aiFindPlayerChr(basechr, chrnum);
	if (chr) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		g_PlayerInvincible = true;
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_invincible_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_invincible chr=%d applied=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_invincible_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerIsInvincible(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 pass = 0;

	if (!s_aiPlayerWeaponStateGraphReady("if_player_is_invincible",
			s_ActiveScenarioGraphs.level_ai_if_player_is_invincible_node_count,
			"missing scenario.ai.condition.if_player_is_invincible node")) {
		return 0;
	}
	chr = s_aiFindPlayerChr(basechr, chrnum);
	if (chr) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		pass = g_PlayerInvincible ? 1 : 0;
		setCurrentPlayerNum(prevplayernum);
	}
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_player_is_invincible_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_is_invincible chr=%d label=%d result=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, label, pass, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_player_is_invincible_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrHasNoGun(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 pass;

	if (!s_aiPlayerWeaponStateGraphReady("if_chr_has_no_gun",
			s_ActiveScenarioGraphs.level_ai_if_chr_has_no_gun_node_count,
			"missing scenario.ai.condition.if_chr_has_no_gun node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	pass = chr && chr->model && chr->gunprop == NULL;
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_chr_has_no_gun_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_has_no_gun chr=%d label=%d result=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, label, pass, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_chr_has_no_gun_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrDeleteWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerWeaponStateGraphReady("chr_delete_weapon",
			s_ActiveScenarioGraphs.level_ai_chr_delete_weapon_node_count,
			"missing scenario.ai.action.chr_delete_weapon node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		weaponDeleteFromChr(chr, weaponnum);
		applied = 1;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_delete_weapon_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_delete_weapon chr=%d weapon=%d applied=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, weaponnum, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_delete_weapon_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTriggerShotList(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken = 0;

	if (!s_aiPlayerWeaponStateGraphReady("if_trigger_shot_list",
			s_ActiveScenarioGraphs
				.level_ai_if_trigger_shot_list_node_count,
			"missing scenario.ai.condition.if_trigger_shot_list node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && (chr->chrflags & CHRCFLAG_TRIGGERSHOTLIST)) {
		chr->chrflags &= ~CHRCFLAG_TRIGGERSHOTLIST;
		branch_taken = 1;
	}
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_trigger_shot_list_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_trigger_shot_list chr=%d label=%d branch=%d source=%s backend=graph.ai.action.player_weapon_state+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_trigger_shot_list_logged = 1;
	}
	return 1;
}

static s32 s_aiPlayerCutsceneGraphReady(const char *action,
	s32 node_count, const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteEndLevel(void)
{
	if (!s_aiPlayerCutsceneGraphReady("end_level",
			s_ActiveScenarioGraphs.level_ai_end_level_node_count,
			"missing scenario.ai.action.end_level node")) {
		return 0;
	}
	if (debugAllowEndLevel()) {
		if (g_Vars.autocutplaying) {
			g_Vars.autocutfinished = true;
		} else {
			func0000e990();
		}
	}
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_end_level_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action end_level autocut=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			g_Vars.autocutplaying ? 1 : 0,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_end_level_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteEndCutscene(void)
{
	if (!s_aiPlayerCutsceneGraphReady("end_cutscene",
			s_ActiveScenarioGraphs.level_ai_end_cutscene_node_count,
			"missing scenario.ai.action.end_cutscene node")) {
		return 0;
	}
	playerEndCutscene();
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_end_cutscene_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action end_cutscene source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_end_cutscene_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteWarpJoToPad(s32 pad_id)
{
	if (!s_aiPlayerCutsceneGraphReady("warp_jo_to_pad",
			s_ActiveScenarioGraphs.level_ai_warp_jo_to_pad_node_count,
			"missing scenario.ai.action.warp_jo_to_pad node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("warp_jo_to_pad",
			"missing pads.tsv source");
	}
	playerPrepareWarpType1((s16)pad_id);
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_warp_jo_to_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action warp_jo_to_pad pad=%d source=%s pads=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv+pads.tsv",
			pad_id, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_warp_jo_to_pad_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetCameraAnimation(s32 anim_id)
{
	if (!s_aiPlayerCutsceneGraphReady("set_camera_animation",
			s_ActiveScenarioGraphs.level_ai_set_camera_animation_node_count,
			"missing scenario.ai.action.set_camera_animation node")) {
		return 0;
	}
	playerStartCutscene((s16)anim_id);
	if (g_Vars.currentplayer->haschrbody == false) {
		g_Vars.chrdata->sleep = -1;
		if (!s_ActiveScenarioGraphs.ai_action_set_camera_animation_logged) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.GRAPH: AI action set_camera_animation anim=%d yielded=1 source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
				anim_id, s_ActiveScenarioGraphs.ai_lists_path);
			s_ActiveScenarioGraphs.ai_action_set_camera_animation_logged = 1;
		}
		return 2;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_set_camera_animation_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_camera_animation anim=%d yielded=0 source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			anim_id, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_camera_animation_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfInCutscene(s32 label)
{
	s32 branch;

	if (!s_aiPlayerCutsceneGraphReady("if_in_cutscene",
			s_ActiveScenarioGraphs.level_ai_if_in_cutscene_node_count,
			"missing scenario.ai.condition.if_in_cutscene node")) {
		return 0;
	}
	branch = playerCurrentCutsceneInProgress() ? 1 : 0;
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_in_cutscene_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_in_cutscene label=%d branch=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			label, branch, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_in_cutscene_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCutsceneButtonPressed(s32 label)
{
	s32 branch;

	if (!s_aiPlayerCutsceneGraphReady("if_cutscene_button_pressed",
			s_ActiveScenarioGraphs.level_ai_if_cutscene_button_pressed_node_count,
			"missing scenario.ai.condition.if_cutscene_button_pressed node")) {
		return 0;
	}
	branch = ((playerAnyCutsceneInProgress() &&
			playerAnyCutsceneSkipRequested()) ||
		(g_Vars.stagenum == STAGE_CITRAINING && var80087260 > 0));
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_cutscene_button_pressed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_cutscene_button_pressed label=%d branch=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			label, branch, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_cutscene_button_pressed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteReorientForCutsceneStop(s32 mode)
{
	if (!s_aiPlayerCutsceneGraphReady("reorient_for_cutscene_stop",
			s_ActiveScenarioGraphs.level_ai_reorient_for_cutscene_stop_node_count,
			"missing scenario.ai.action.reorient_for_cutscene_stop node")) {
		return 0;
	}
	playerReorientForCutsceneStop(mode);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_reorient_for_cutscene_stop_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action reorient_for_cutscene_stop mode=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			mode, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_reorient_for_cutscene_stop_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteWarpJoToTag(s32 tag_id, s32 arg0, s32 arg1)
{
	struct tag *tag;
	s32 applied = 0;

	if (!s_aiPlayerCutsceneGraphReady("warp_jo_to_tag",
			s_ActiveScenarioGraphs.level_ai_warp_jo_to_tag_node_count,
			"missing scenario.ai.action.warp_jo_to_tag node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("warp_jo_to_tag",
			"missing objects.tsv source");
	}
	tag = tagFindById(tag_id);
	if (tag) {
		s32 cmdindex = setupGetCmdIndexByTag(tag);

		if (cmdindex >= 0) {
			struct warpparams *params =
				(struct warpparams *)setupGetCmdByIndex(
					cmdindex + tag->cmdoffset);
			playerPrepareWarpType2(params, arg0, arg1);
			applied = 1;
		}
	}
	g_Vars.aioffset += 7;
	if (!s_ActiveScenarioGraphs.ai_action_warp_jo_to_tag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action warp_jo_to_tag tag=%d arg0=%d arg1=%d applied=%d source=%s objects=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv+objects.tsv",
			tag_id, arg0, arg1, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_warp_jo_to_tag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRevokeControl(struct chrdata *basechr,
	s32 chrnum, s32 flags)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerCutsceneGraphReady("revoke_control",
			s_ActiveScenarioGraphs.level_ai_revoke_control_node_count,
			"missing scenario.ai.action.revoke_control node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		bgunSetSightVisible(GUNSIGHTREASON_NOCONTROL, false);
		bgunSetGunAmmoVisible(GUNAMMOREASON_NOCONTROL, false);
		if ((flags & 2) == 0) {
			hudmsgsSetOff(HUDMSGREASON_NOCONTROL);
		}
		if ((flags & 4) == 0) {
			countdownTimerSetVisible(COUNTDOWNTIMERREASON_NOCONTROL,
				false);
		}
		g_PlayersWithControl[g_Vars.currentplayernum] = false;
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_revoke_control_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action revoke_control chr=%d flags=0x%02x applied=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			chrnum, flags, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_revoke_control_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteGrantControl(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerCutsceneGraphReady("grant_control",
			s_ActiveScenarioGraphs.level_ai_grant_control_node_count,
			"missing scenario.ai.action.grant_control node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		setCurrentPlayerNum(playermgrGetPlayerNumByProp(chr->prop));
		bgunSetSightVisible(GUNSIGHTREASON_NOCONTROL, true);
		bgunSetGunAmmoVisible(GUNAMMOREASON_NOCONTROL, true);
		hudmsgsSetOn(HUDMSGREASON_NOCONTROL);
		countdownTimerSetVisible(COUNTDOWNTIMERREASON_NOCONTROL, true);
		g_PlayersWithControl[g_Vars.currentplayernum] = true;
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_grant_control_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action grant_control chr=%d applied=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_grant_control_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayerFadeIn(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiPlayerCutsceneGraphReady("player_fade_in",
			s_ActiveScenarioGraphs.level_ai_player_fade_in_node_count,
			"missing scenario.ai.action.player_fade_in node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		if (var8007074c != 2) {
			playerSetFadeColour(0, 0, 0, 0);
			playerSetFadeFrac(60, 1);
			applied = 1;
		}
		setCurrentPlayerNum(prevplayernum);
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_player_fade_in_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action player_fade_in chr=%d applied=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_player_fade_in_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayersFadeOut(void)
{
	s32 playernum;
	u32 prevplayernum;
	s32 applied = 0;

	if (!s_aiPlayerCutsceneGraphReady("players_fade_out",
			s_ActiveScenarioGraphs.level_ai_players_fade_out_node_count,
			"missing scenario.ai.action.players_fade_out node")) {
		return 0;
	}
	prevplayernum = g_Vars.currentplayernum;
	for (playernum = 0; playernum < PLAYERCOUNT(); playernum++) {
		setCurrentPlayerNum(playernum);
		if (var8007074c != 2) {
			playerSetFadeColour(0, 0, 0, 1);
			playerSetFadeFrac(60, 0);
			applied++;
		}
	}
	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_players_fade_out_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action players_fade_out applied=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_players_fade_out_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfColourFadeComplete(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 pass = 0;

	if (!s_aiPlayerCutsceneGraphReady("if_colour_fade_complete",
			s_ActiveScenarioGraphs.level_ai_if_colour_fade_complete_node_count,
			"missing scenario.ai.condition.if_colour_fade_complete node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		pass = g_Vars.players[playernum]->colourfadetimemax60 < 0;
	}
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_colour_fade_complete_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_colour_fade_complete chr=%d label=%d branch=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			chrnum, label, pass, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_colour_fade_complete_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePrepareWarpOrbit(s32 range, s32 height1,
	s32 rotangle, s32 padnum, s32 height2, s32 posangle)
{
	if (!s_aiPlayerCutsceneGraphReady("prepare_warp_orbit",
			s_ActiveScenarioGraphs.level_ai_prepare_warp_orbit_node_count,
			"missing scenario.ai.action.prepare_warp_orbit node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("prepare_warp_orbit",
			"missing pads.tsv source");
	}
	playerPrepareWarpType3(posangle * M_BADTAU / 65536,
		rotangle * M_BADTAU / 65536, range, height1, height2, padnum);
	g_Vars.aioffset += 14;
	if (!s_ActiveScenarioGraphs.ai_action_prepare_warp_orbit_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action prepare_warp_orbit pad=%d range=%d source=%s pads=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv+pads.tsv",
			padnum, range, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_prepare_warp_orbit_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteBeginWarpLatch(void)
{
	if (!s_aiPlayerCutsceneGraphReady("begin_warp_latch",
			s_ActiveScenarioGraphs.level_ai_begin_warp_latch_node_count,
			"missing scenario.ai.action.begin_warp_latch node")) {
		return 0;
	}
	var8007073c = 1;
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_begin_warp_latch_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action begin_warp_latch source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_begin_warp_latch_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfWarpLatchComplete(s32 label)
{
	s32 branch;

	if (!s_aiPlayerCutsceneGraphReady("if_warp_latch_complete",
			s_ActiveScenarioGraphs.level_ai_if_warp_latch_complete_node_count,
			"missing scenario.ai.condition.if_warp_latch_complete node")) {
		return 0;
	}
	branch = var8007073c == 2;
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_warp_latch_complete_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_warp_latch_complete label=%d branch=%d source=%s backend=graph.ai.action.player_cutscene+ai/ailists.tsv",
			label, branch, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_warp_latch_complete_logged = 1;
	}
	return 1;
}

static s32 s_aiSetupSpawnGraphReady(const char *action,
	s32 node_count, const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSpawnChrAtPad(struct chrdata *basechr,
	s32 body, s32 head, s32 pad, u16 ailistid, u32 spawnflags,
	s32 label)
{
	u8 *ailist;
	struct prop *prop;
	s32 pass = 0;

	if (!s_aiSetupSpawnGraphReady("spawn_chr_at_pad",
			s_ActiveScenarioGraphs.level_ai_spawn_chr_at_pad_node_count,
			"missing scenario.ai.action.spawn_chr_at_pad node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("spawn_chr_at_pad",
			"missing pads.tsv source");
	}
	ailist = ailistFindById(ailistid);
	prop = chrSpawnAtPad(basechr, body, (s8)head, pad, ailist,
		spawnflags);
	pass = prop != NULL;
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 13;
	}
	if (!s_ActiveScenarioGraphs.ai_action_spawn_chr_at_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action spawn_chr_at_pad body=%d head=%d pad=%d ailist=%u pass=%d source=%s pads=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv+pads.tsv",
			body, head, pad, ailistid, pass,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_spawn_chr_at_pad_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSpawnChrAtChr(struct chrdata *basechr,
	s32 body, s32 head, s32 chrnum, u16 ailistid, u32 spawnflags,
	s32 label)
{
	u8 *ailist;
	struct prop *prop;
	s32 pass = 0;

	if (!s_aiSetupSpawnGraphReady("spawn_chr_at_chr",
			s_ActiveScenarioGraphs.level_ai_spawn_chr_at_chr_node_count,
			"missing scenario.ai.action.spawn_chr_at_chr node")) {
		return 0;
	}
	ailist = ailistFindById(ailistid);
	prop = chrSpawnAtChr(basechr, body, (s8)head, chrnum, ailist,
		spawnflags);
	pass = prop != NULL;
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 12;
	}
	if (!s_ActiveScenarioGraphs.ai_action_spawn_chr_at_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action spawn_chr_at_chr body=%d head=%d chr=%d ailist=%u pass=%d source=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv",
			body, head, chrnum, ailistid, pass,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_spawn_chr_at_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryEquipWeapon(u32 model, s32 weaponnum,
	u32 flags, s32 label)
{
	struct prop *prop = NULL;
	u32 applied_flags = flags;

	if (!s_aiSetupSpawnGraphReady("try_equip_weapon",
			s_ActiveScenarioGraphs.level_ai_try_equip_weapon_node_count,
			"missing scenario.ai.action.try_equip_weapon node")) {
		return 0;
	}
	if (g_Vars.chrdata && g_Vars.chrdata->prop && g_Vars.chrdata->model) {
#if VERSION < VERSION_NTSC_1_0
		if (cheatIsActive(CHEAT_MARQUIS) && g_Vars.stagenum != STAGE_MBR) {
			if (g_Vars.stagenum == STAGE_INVESTIGATION &&
					lvGetDifficulty() == DIFF_PA &&
					weaponnum == WEAPON_K7AVENGER) {
				prop = chrGiveWeapon(g_Vars.chrdata, model,
					weaponnum, applied_flags);
			}
		}
#elif VERSION < VERSION_PAL_BETA
		if (cheatIsActive(CHEAT_MARQUIS)) {
			applied_flags &= ~OBJFLAG_WEAPON_LEFTHANDED;
			applied_flags |= OBJFLAG_WEAPON_AICANNOTUSE;
			prop = chrGiveWeapon(g_Vars.chrdata, model, weaponnum,
				applied_flags);
		}
#else
		if (cheatIsActive(CHEAT_MARQUIS)) {
			if (g_Vars.chrdata->bodynum != BODY_CASSANDRA ||
					mainGetStageNum() != STAGE_MBR) {
				applied_flags &= ~OBJFLAG_WEAPON_LEFTHANDED;
				applied_flags |= OBJFLAG_WEAPON_AICANNOTUSE;
			}
			prop = chrGiveWeapon(g_Vars.chrdata, model, weaponnum,
				applied_flags);
		}
#endif
		else if (cheatIsActive(CHEAT_ENEMYROCKETS)) {
			switch (weaponnum) {
			case WEAPON_FALCON2:
			case WEAPON_FALCON2_SILENCER:
			case WEAPON_FALCON2_SCOPE:
			case WEAPON_MAGSEC4:
			case WEAPON_MAULER:
			case WEAPON_PHOENIX:
			case WEAPON_DY357MAGNUM:
			case WEAPON_DY357LX:
			case WEAPON_CMP150:
			case WEAPON_CYCLONE:
			case WEAPON_CALLISTO:
			case WEAPON_RCP120:
			case WEAPON_LAPTOPGUN:
			case WEAPON_DRAGON:
			case WEAPON_AR34:
			case WEAPON_SUPERDRAGON:
			case WEAPON_SHOTGUN:
			case WEAPON_REAPER:
			case WEAPON_SNIPERRIFLE:
			case WEAPON_FARSIGHT:
			case WEAPON_DEVASTATOR:
			case WEAPON_ROCKETLAUNCHER:
			case WEAPON_SLAYER:
			case WEAPON_COMBATKNIFE:
			case WEAPON_CROSSBOW:
			case WEAPON_TRANQUILIZER:
			case WEAPON_GRENADE:
			case WEAPON_NBOMB:
			case WEAPON_TIMEDMINE:
			case WEAPON_PROXIMITYMINE:
			case WEAPON_REMOTEMINE:
				prop = chrGiveWeapon(g_Vars.chrdata,
					MODEL_CHRDYROCKET, WEAPON_ROCKETLAUNCHER,
					applied_flags);
				break;
			case WEAPON_K7AVENGER:
				if (g_Vars.stagenum == STAGE_INVESTIGATION &&
						lvGetDifficulty() == DIFF_PA) {
					prop = chrGiveWeapon(g_Vars.chrdata, model,
						weaponnum, applied_flags);
				} else {
					prop = chrGiveWeapon(g_Vars.chrdata,
						MODEL_CHRDYROCKET,
						WEAPON_ROCKETLAUNCHER,
						applied_flags);
				}
				break;
			default:
				prop = chrGiveWeapon(g_Vars.chrdata, model, weaponnum,
					applied_flags);
				break;
			}
		} else {
			prop = chrGiveWeapon(g_Vars.chrdata, model, weaponnum,
				applied_flags);
		}
	}
	if (prop) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 10;
	}
	if (!s_ActiveScenarioGraphs.ai_action_try_equip_weapon_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_equip_weapon model=%u weapon=%d flags=0x%08x pass=%d source=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv",
			model, weaponnum, applied_flags, prop != NULL,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_try_equip_weapon_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteTryEquipHat(u32 modelnum, u32 flags,
	s32 label)
{
	struct prop *prop = NULL;

	if (!s_aiSetupSpawnGraphReady("try_equip_hat",
			s_ActiveScenarioGraphs.level_ai_try_equip_hat_node_count,
			"missing scenario.ai.action.try_equip_hat node")) {
		return 0;
	}
	if (g_Vars.chrdata && g_Vars.chrdata->prop && g_Vars.chrdata->model) {
		prop = hatCreateForChr(g_Vars.chrdata, modelnum, flags);
	}
	if (prop) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 9;
	}
	if (!s_ActiveScenarioGraphs.ai_action_try_equip_hat_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action try_equip_hat model=%u flags=0x%08x pass=%d source=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv",
			modelnum, flags, prop != NULL,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_try_equip_hat_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetObjImage(s32 tag_id, s32 slot,
	s32 image)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiSetupSpawnGraphReady("set_obj_image",
			s_ActiveScenarioGraphs.level_ai_set_obj_image_node_count,
			"missing scenario.ai.action.set_obj_image node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("set_obj_image",
			"missing objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		if (obj->type == OBJTYPE_SINGLEMONITOR) {
			struct singlemonitorobj *sm = (struct singlemonitorobj *)obj;
			tvscreenSetImageByNum(&sm->screen, image);
			applied = 1;
		} else if (obj->type == OBJTYPE_MULTIMONITOR) {
			struct multimonitorobj *mm = (struct multimonitorobj *)obj;
			if ((u32)slot < ARRAYCOUNT(mm->screens)) {
				tvscreenSetImageByNum(&mm->screens[slot], image);
				applied = 1;
			}
		}
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_set_obj_image_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_obj_image tag=%d slot=%d image=%d applied=%d source=%s objects=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv+objects.tsv",
			tag_id, slot, image, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_set_obj_image_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteObjectDoAnimation(s32 anim_id, s32 tag_id,
	s32 speed_divisor, s32 startframe)
{
	struct defaultobj *obj = NULL;
	f32 speed;
	f32 fstartframe;
	s32 applied = 0;

	if (!s_aiSetupSpawnGraphReady("object_do_animation",
			s_ActiveScenarioGraphs.level_ai_object_do_animation_node_count,
			"missing scenario.ai.action.object_do_animation node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("object_do_animation",
			"missing objects.tsv source");
	}
	if (startframe == 0xffff) {
		fstartframe = 0;
	} else if (startframe == 0xfffe) {
		fstartframe = animGetNumFrames(anim_id) - 2;
		if (fstartframe < 0) {
			fstartframe = 0;
		}
	} else {
		fstartframe = startframe;
	}
	if (tag_id == 255) {
		if (g_Vars.chrdata && g_Vars.chrdata->myspecial >= 0) {
			obj = objFindByTagId(g_Vars.chrdata->myspecial);
		}
	} else {
		obj = objFindByTagId(tag_id);
	}
	if (obj && obj->prop) {
		if (obj->model->anim == NULL) {
			obj->model->anim = modelmgrInstantiateAnim();
		}
		if (obj->model->anim) {
			speed = 1.0f / (s32)speed_divisor;
			if (playerCurrentCutsceneInProgress() &&
					startframe != 0xfffe) {
#if PAL
				fstartframe += var8009e388pf * speed;
#else
				fstartframe += g_CutsceneFrameOverrun240 * speed *
					0.25f;
#endif
			}
			animInit(obj->model->anim);
#if VERSION >= VERSION_JPN_FINAL
			modelSetAnimPlaySpeed(obj->model, 1, 0);
#elif VERSION >= VERSION_PAL_BETA
			modelSetAnimPlaySpeed(obj->model, 1.2, 0);
#endif
			modelSetAnimation(obj->model, anim_id, 0, fstartframe,
				speed, 0);
			modelSetAnimScale(obj->model,
				bgGetStageTranslationThing() * obj->model->scale *
				100.0f);
			applied = 1;
		}
	}
	g_Vars.aioffset += 8;
	if (!s_ActiveScenarioGraphs.ai_action_object_do_animation_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action object_do_animation anim=%d tag=%d speed_divisor=%d startframe=%d applied=%d source=%s objects=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv+objects.tsv",
			anim_id, tag_id, speed_divisor, startframe, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_object_do_animation_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetDoorOpen(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiSetupSpawnGraphReady("set_door_open",
			s_ActiveScenarioGraphs.level_ai_set_door_open_node_count,
			"missing scenario.ai.action.set_door_open node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("set_door_open",
			"missing objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		struct doorobj *door = (struct doorobj *)obj;
		door->frac = door->maxfrac;
		door->fracspeed = 0;
		door->lastopen60 = g_Vars.lvframe60;
		door->mode = 0;
		doorUpdateTiles(door);
		doorActivatePortal(door);
		psStopSound(door->base.prop, PSTYPE_GENERAL, 0xffff);
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_set_door_open_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_door_open tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.setup_spawn+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_set_door_open_logged = 1;
	}
	return 1;
}

static s32 s_aiEntityLifecycleGraphReady(const char *action,
	s32 node_count, const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDuplicateChr(struct chrdata *basechr,
	s32 chrnum, u16 ailistid, u32 spawnflags, s32 label)
{
	u8 *ailist;
	struct chrdata *chr;
	struct chrdata *clone = NULL;
	struct prop *cloneprop = NULL;
	struct prop *srcweapon0prop = NULL;
	struct prop *srcweapon1prop = NULL;
	struct prop *cloneweapon0prop = NULL;
	struct prop *cloneweapon1prop = NULL;
	struct weaponobj *srcweapon0 = NULL;
	struct weaponobj *srcweapon1 = NULL;
	struct weaponobj *cloneweapon0 = NULL;
	struct weaponobj *cloneweapon1 = NULL;
	s32 pass = 0;

	if (!s_aiEntityLifecycleGraphReady("duplicate_chr",
			s_ActiveScenarioGraphs.level_ai_duplicate_chr_node_count,
			"missing scenario.ai.action.duplicate_chr node")) {
		return 0;
	}
	ailist = ailistFindById(ailistid);
	chr = chrFindById(basechr, chrnum);
	if (chr && (chr->chrflags & CHRCFLAG_CLONEABLE)) {
		cloneprop = chrSpawnAtChr(basechr, chr->bodynum, -1,
			chr->chrnum, ailist, spawnflags);
		if (cloneprop) {
			clone = cloneprop->chr;
			chrSetChrnum(clone, chrsGetNextUnusedChrnum());
			chr->chrdup = clone->chrnum;
			srcweapon0prop = chrGetHeldProp(chr, 0);
			if (srcweapon0prop) {
				srcweapon0 = srcweapon0prop->weapon;
				cloneweapon0prop = chrGiveWeapon(clone,
					srcweapon0->base.modelnum,
					srcweapon0->weaponnum, 0);
				if (cloneweapon0prop) {
					cloneweapon0 = cloneweapon0prop->weapon;
				}
			}
			srcweapon1prop = chrGetHeldProp(chr, 1);
			if (srcweapon1prop) {
				srcweapon1 = srcweapon1prop->weapon;
				cloneweapon1prop = chrGiveWeapon(clone,
					srcweapon1->base.modelnum,
					srcweapon1->weaponnum,
					OBJFLAG_WEAPON_LEFTHANDED);
				if (cloneweapon1prop) {
					cloneweapon1 = cloneweapon1prop->weapon;
				}
			}
			if (srcweapon1 && srcweapon0 && cloneweapon1 &&
					cloneweapon0 &&
					srcweapon0 == srcweapon1->dualweapon &&
					srcweapon1 == srcweapon0->dualweapon) {
				propweaponSetDual(cloneweapon1, cloneweapon0);
			}
			if (chr->weapons_held[2]) {
				struct defaultobj *obj = chr->weapons_held[2]->obj;
				hatCreateForChr(clone, obj->modelnum, 0);
			}
			clone->flags = chr->flags;
			clone->flags2 = chr->flags2;
			clone->padpreset1 = chr->padpreset1;
			if (g_Vars.normmplayerisrunning == false &&
					g_MissionConfig.iscoop &&
					g_Vars.numaibuddies > 0) {
				clone->flags |= CHRFLAG0_AIVSAI;
			}
			if (spawnflags & SPAWNFLAG_HIDDEN) {
				clone->chrflags &= CHRCFLAG_HIDDEN;
			}
			clone->team = chr->team;
			clone->squadron = chr->squadron;
			clone->voicebox = chr->voicebox;
			rebuildTeams();
			rebuildSquadrons();
			pass = 1;
		}
	}
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 10;
	}
	if (!s_ActiveScenarioGraphs.ai_action_duplicate_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action duplicate_chr chr=%d ailist=%u pass=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			chrnum, ailistid, pass, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_duplicate_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteEnableChr(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("enable_chr",
			s_ActiveScenarioGraphs.level_ai_enable_chr_node_count,
			"missing scenario.ai.action.enable_chr node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->model) {
		propActivate(chr->prop);
		propEnable(chr->prop);
		chr0f0220ac(chr);
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_enable_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action enable_chr chr=%d applied=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_enable_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDisableChr(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("disable_chr",
			s_ActiveScenarioGraphs.level_ai_disable_chr_node_count,
			"missing scenario.ai.action.disable_chr node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->model) {
		propDeregisterRooms(chr->prop);
		propDelist(chr->prop);
		propDisable(chr->prop);
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_disable_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action disable_chr chr=%d applied=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_disable_chr_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteEnableObj(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("enable_obj",
			s_ActiveScenarioGraphs.level_ai_enable_obj_node_count,
			"missing scenario.ai.action.enable_obj node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("enable_obj",
			"missing objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->model) {
		propActivate(obj->prop);
		propEnable(obj->prop);
		if (g_Vars.currentplayer->eyespy == NULL &&
				obj->type == OBJTYPE_WEAPON) {
			struct weaponobj *weapon = (struct weaponobj *)obj;
			if (weapon->weaponnum == WEAPON_EYESPY) {
				playerInitEyespy();
			}
		}
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_enable_obj_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action enable_obj tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_enable_obj_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDisableObj(s32 tag_id)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("disable_obj",
			s_ActiveScenarioGraphs.level_ai_disable_obj_node_count,
			"missing scenario.ai.action.disable_obj node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("disable_obj",
			"missing objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->model) {
#if VERSION >= VERSION_PAL_FINAL
		if (g_Vars.autocutplaying &&
				mainGetStageNum() == STAGE_AIRFORCEONE &&
				(obj->modelnum == MODEL_AIRFORCE1 ||
					obj->modelnum == MODEL_SK_SHUTTLE)) {
			applied = 0;
		} else
#endif
		if (obj->prop->parent) {
			objDetach(obj->prop);
			applied = 1;
		} else {
			propDeregisterRooms(obj->prop);
			propDelist(obj->prop);
			propDisable(obj->prop);
			applied = 1;
		}
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_disable_obj_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action disable_obj tag=%d applied=%d source=%s objects=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv+objects.tsv",
			tag_id, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_disable_obj_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrMoveToPad(struct chrdata *basechr,
	s32 chrnum, s32 pad_or_chr, s32 mode, s32 label)
{
	struct chrdata *chr;
	s32 pass = 0;
	f32 theta;
	struct pad pad;
	RoomNum rooms[2];

	if (!s_aiEntityLifecycleGraphReady("chr_move_to_pad",
			s_ActiveScenarioGraphs.level_ai_chr_move_to_pad_node_count,
			"missing scenario.ai.action.chr_move_to_pad node")) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("chr_move_to_pad",
			"missing pads.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop) {
#if VERSION >= VERSION_NTSC_1_0
		if (mode == 88) {
			struct chrdata *chr2 = chrFindById(basechr, pad_or_chr & 0xff);
			if (chr2 && chr2->prop) {
				theta = chrGetInverseTheta(chr2);
				pass = chrMoveToPos(chr, &chr2->prop->pos,
					chr2->prop->rooms, theta, false);
			}
		} else
#endif
		{
			s32 padnum = chrResolvePadId(chr, pad_or_chr);
			if (padnum >= 0) {
				padUnpack(padnum, PADFIELD_POS | PADFIELD_LOOK |
					PADFIELD_ROOM, &pad);
				theta = atan2f(pad.look.x, pad.look.z);
				rooms[0] = pad.room;
				rooms[1] = -1;
				pass = chrMoveToPos(chr, &pad.pos, rooms, theta,
					mode);
			}
		}
	}
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 7;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_move_to_pad_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_move_to_pad chr=%d operand=%d mode=%d pass=%d source=%s pads=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv+pads.tsv",
			chrnum, pad_or_chr, mode, pass,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_chr_move_to_pad_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetTeam(struct chrdata *basechr,
	s32 chrnum, s32 team)
{
	u32 playernum;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("chr_set_team",
			s_ActiveScenarioGraphs.level_ai_chr_set_team_node_count,
			"missing scenario.ai.action.chr_set_team node")) {
		return 0;
	}
	if (chrnum == CHR_ANTI && g_Vars.antiplayernum >= 0) {
		for (playernum = 0; playernum < PLAYERCOUNT(); playernum++) {
			struct player *player = g_Vars.players[playernum];
			if (player && PLAYER_IS_ANTI(player)) {
				struct chrdata *chr = player->prop->chr;
				if (chr) {
					chr->team = team;
					applied++;
				}
			}
		}
	} else {
		struct chrdata *chr = chrFindById(basechr, chrnum);
		if (chr) {
			chr->team = team;
			applied = 1;
		}
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_team_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_team chr=%d team=%d applied=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			chrnum, team, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_team_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDamageChrByAmount(struct chrdata *basechr,
	s32 chrnum, s32 amount, s32 mode)
{
	struct coord coord = {0, 0, 0};
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("damage_chr_by_amount",
			s_ActiveScenarioGraphs.level_ai_damage_chr_by_amount_node_count,
			"missing scenario.ai.action.damage_chr_by_amount node")) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop) {
		if (mode == 2) {
			struct gset gset = {WEAPON_COMBATKNIFE, 0, 0, FUNC_POISON};
			chrDamageByMisc(chr, (s32)amount * 0.03125f,
				&coord, &gset, NULL);
		} else if (mode == 0) {
			chrDamageByMisc(chr, (s32)amount * 0.03125f,
				&coord, NULL, NULL);
		} else {
			chrDamageByMisc(chr, (s32)amount * -0.03125f,
				&coord, NULL, NULL);
		}
		applied = 1;
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_damage_chr_by_amount_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action damage_chr_by_amount chr=%d amount=%d mode=%d applied=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			chrnum, amount, mode, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_damage_chr_by_amount_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDoPresetAnimation(struct chrdata *chr,
	s32 preset)
{
	u16 anims[] = {
		ANIM_0296, ANIM_0297, ANIM_0298, ANIM_028A, ANIM_028C,
		ANIM_0290, ANIM_0291, ANIM_TALKING_00A3, ANIM_028E,
		ANIM_028F, ANIM_TALKING_0231, ANIM_TALKING_0232,
		ANIM_TALKING_0233, ANIM_TALKING_0234, ANIM_028D,
	};
	s32 applied = 0;

	if (!s_aiEntityLifecycleGraphReady("do_preset_animation",
			s_ActiveScenarioGraphs.level_ai_do_preset_animation_node_count,
			"missing scenario.ai.action.do_preset_animation node")) {
		return 0;
	}
	if (chr) {
		if (preset == 255) {
			chrTryStartAnim(chr, anims[7 + (rngRandom() % 8)], 0, -1,
				0, 15, 0.5);
		} else if (preset == 254) {
			struct prop *prop0 = chrGetHeldProp(chr, 1);
			struct prop *prop1 = chrGetHeldProp(chr, 0);
			if (weaponIsOneHanded(prop0) || weaponIsOneHanded(prop1)) {
				chrTryStartAnim(chr, ANIM_FIX_GUN_JAM_EASY, 0, -1,
					0, 5, 0.5);
			} else {
				chrTryStartAnim(chr, ANIM_FIX_GUN_JAM_HARD, 0, -1,
					0, 5, 0.5);
			}
		} else if (preset == 3) {
			chrTryStartAnim(chr, anims[3 + (rngRandom() & 1)], 0, -1,
				0, 15, 0.5);
		} else if (preset >= 0 && preset < (s32)(sizeof(anims) / sizeof(anims[0]))) {
			chrTryStartAnim(chr, anims[preset], 0, -1, 0, 15, 0.5);
		}
		applied = 1;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_do_preset_animation_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action do_preset_animation preset=%d applied=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			preset, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_do_preset_animation_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan(
	struct chrdata *chr, s32 label)
{
	f32 distance = 3000;
	s32 branch = 0;

	if (!s_aiEntityLifecycleGraphReady("if_player_chr_portal_distance_less_than",
			s_ActiveScenarioGraphs.level_ai_if_player_chr_portal_distance_less_than_node_count,
			"missing scenario.ai.condition.if_player_chr_portal_distance_less_than node")) {
		return 0;
	}
	if (chr && chr->prop && g_Vars.currentplayer &&
			g_Vars.currentplayer->prop) {
		func0f0056f4(g_Vars.currentplayer->prop->rooms[0],
			&g_Vars.currentplayer->prop->pos, chr->prop->rooms[0],
			&chr->prop->pos, 0, &distance, 0);
		branch = distance < 3000;
	}
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_player_chr_portal_distance_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_chr_portal_distance_less_than label=%d branch=%d distance=%.2f source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			label, branch, distance, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_player_chr_portal_distance_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrRepositionValid(struct chrdata *chr,
	s32 label)
{
	s32 branch = 0;

	if (!s_aiEntityLifecycleGraphReady("if_chr_reposition_valid",
			s_ActiveScenarioGraphs.level_ai_if_chr_reposition_valid_node_count,
			"missing scenario.ai.condition.if_chr_reposition_valid node")) {
		return 0;
	}
	if (chr && chr->prop &&
			chr0f01f264(chr, &chr->prop->pos, chr->prop->rooms,
				0, false)) {
		branch = 1;
	}
	if (branch) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_reposition_valid_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_reposition_valid label=%d branch=%d source=%s backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv",
			label, branch, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_reposition_valid_logged = 1;
	}
	return 1;
}

static s32 s_aiGunInteractionGraphReady(const char *action,
	s32 node_count, const char *missing_reason)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		return s_aiGraphRuntimeFailure(action, missing_reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing objects.tsv source");
	}
	if (!s_ActiveScenarioGraphs.level_graph_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing level.graph.json source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteDoGunCommand(struct chrdata *chr,
	s32 mode, s32 label)
{
	struct weaponobj *weapon;
	s32 branch_taken = 0;
	s32 moved_to_gun = 0;

	if (!s_aiGunInteractionGraphReady("do_gun_command",
			s_ActiveScenarioGraphs.level_ai_do_gun_command_node_count,
			"missing scenario.ai.action.do_gun_command node")) {
		return 0;
	}
	if (!chr || !chr->gunprop || !chr->gunprop->weapon) {
		return s_aiGraphRuntimeFailure("do_gun_command",
			"missing current chr gun prop");
	}
	weapon = chr->gunprop->weapon;
	if (mode == 0 || ((weapon->base.hidden & OBJHFLAG_PROJECTILE) == 0 &&
			mode == 1)) {
		if (mode == 0) {
			chrGoToProp(chr, chr->gunprop, GOPOSFLAG_JOG);
			moved_to_gun = 1;
		}
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
		branch_taken = 1;
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_do_gun_command_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action do_gun_command mode=%d label=%d branch=%d moved=%d source=%s objects=%s backend=graph.ai.action.gun_interaction+ai/ailists.tsv+objects.tsv+scene.glb",
			mode, label, branch_taken, moved_to_gun,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_do_gun_command_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDistanceToGunLessThan(struct chrdata *chr,
	f32 distance, s32 label)
{
	f32 xdiff = 0;
	f32 ydiff = 0;
	f32 zdiff = 0;
	s32 pass;

	if (!s_aiGunInteractionGraphReady("if_distance_to_gun_less_than",
			s_ActiveScenarioGraphs.level_ai_if_distance_to_gun_less_than_node_count,
			"missing scenario.ai.condition.if_distance_to_gun_less_than node")) {
		return 0;
	}
	if (chr && chr->gunprop && chr->prop) {
		xdiff = chr->prop->pos.x - chr->gunprop->pos.x;
		ydiff = chr->prop->pos.y - chr->gunprop->pos.y;
		zdiff = chr->prop->pos.z - chr->gunprop->pos.z;
	}
	pass = ydiff < 200 && ydiff > -200 &&
		xdiff < distance && xdiff > -distance &&
		zdiff < distance && zdiff > -distance;
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs
			.ai_action_if_distance_to_gun_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_distance_to_gun_less_than distance=%.2f label=%d result=%d source=%s objects=%s backend=graph.ai.action.gun_interaction+ai/ailists.tsv+objects.tsv+scene.glb",
			(double)distance, label, pass,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_distance_to_gun_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRecoverGun(struct chrdata *chr, s32 label)
{
	struct prop *prop;
	s32 recovered = 0;

	if (!s_aiGunInteractionGraphReady("recover_gun",
			s_ActiveScenarioGraphs.level_ai_recover_gun_node_count,
			"missing scenario.ai.action.recover_gun node")) {
		return 0;
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("recover_gun", "missing chr");
	}
	prop = chr->gunprop;
	chr->gunprop = NULL;
	if (prop && prop->obj && prop->parent == NULL &&
			prop->type == PROPTYPE_WEAPON) {
		propDeregisterRooms(prop);
		propDelist(prop);
		propDisable(prop);
		chrEquipWeapon(prop->weapon, chr);
		recovered = 1;
	}
	g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
		g_Vars.aioffset, label);
	if (!s_ActiveScenarioGraphs.ai_action_recover_gun_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action recover_gun label=%d recovered=%d source=%s objects=%s backend=graph.ai.action.gun_interaction+ai/ailists.tsv+objects.tsv+scene.glb",
			label, recovered, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_recover_gun_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrCopyProperties(struct chrdata *basechr,
	s32 src_chrnum, s32 label)
{
	struct chrdata *src;
	s32 copied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_copy_properties_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_copy_properties",
			"missing scenario.ai.action.chr_copy_properties node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_copy_properties",
			"missing ai/ailists.tsv source");
	}
	if (!basechr) {
		return s_aiGraphRuntimeFailure("chr_copy_properties",
			"missing destination chr");
	}
	src = chrFindById(basechr, src_chrnum);
	if (src && src->model) {
		basechr->hearingscale = src->hearingscale;
		basechr->visionrange = src->visionrange;
		basechr->padpreset1 = src->padpreset1;
		basechr->chrpreset1 = src->chrpreset1;
		basechr->flags = src->flags;
		basechr->flags2 = src->flags2;
		basechr->team = src->team;
		basechr->squadron = src->squadron;
		basechr->naturalanim = src->naturalanim;
		basechr->myspecial = src->myspecial;
		basechr->yvisang = src->yvisang;
		basechr->teamscandist = src->teamscandist;
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
		copied = 1;
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_copy_properties_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_copy_properties src_chr=%d label=%d copied=%d source=%s backend=graph.ai.action.character_property+ai/ailists.tsv",
			src_chrnum, label, copied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_copy_properties_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePlayerAutoWalk(struct chrdata *basechr,
	s32 chrnum, s32 pad_id, s32 walkspeed, s32 turnspeed, s32 lookup,
	s32 dist)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_player_auto_walk_node_count != 1) {
		return s_aiGraphRuntimeFailure("player_auto_walk",
			"missing scenario.ai.action.player_auto_walk node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure("player_auto_walk",
			"missing ai/ailists.tsv or pads.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		playerAutoWalk((s16)pad_id, (u8)walkspeed, (u8)turnspeed,
			(u8)lookup, (u8)dist);
		setCurrentPlayerNum(prevplayernum);
		applied = 1;
	}
	g_Vars.aioffset += 9;
	if (!s_ActiveScenarioGraphs.ai_action_player_auto_walk_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action player_auto_walk chr=%d pad=%d applied=%d source=%s pads=%s backend=graph.ai.action.player_navigation+ai/ailists.tsv+pads.tsv",
			chrnum, pad_id, applied,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs.ai_action_player_auto_walk_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 walking = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_player_auto_walk_finished_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_player_auto_walk_finished",
			"missing scenario.ai.condition.if_player_auto_walk_finished node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_player_auto_walk_finished",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		if (g_Vars.tickmode == TICKMODE_AUTOWALK) {
			walking = 1;
		}
		setCurrentPlayerNum(prevplayernum);
	}
	if (walking) {
		g_Vars.aioffset += 4;
	} else {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_player_auto_walk_finished_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_auto_walk_finished chr=%d label=%d walking=%d source=%s backend=graph.ai.action.player_navigation+ai/ailists.tsv+pads.tsv",
			chrnum, label, walking,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_player_auto_walk_finished_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjInRoom(struct chrdata *basechr,
	s32 tag_id, s32 room_id, s32 label)
{
	struct defaultobj *obj;
	s32 resolved_room;
	s32 pass;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_obj_in_room_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_obj_in_room",
			"missing scenario.ai.condition.if_obj_in_room node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_obj_in_room",
			"missing ai/ailists.tsv source");
	}
	if (!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("if_obj_in_room",
			"missing objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	resolved_room = chrGetPadRoom(basechr, room_id);
	pass = resolved_room >= 0 && obj && obj->prop &&
		resolved_room == obj->prop->rooms[0];
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 6;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_obj_in_room_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_obj_in_room tag=%d room=%d resolved_room=%d label=%d result=%d source=%s objects=%s backend=graph.ai.condition.object_room+ai/ailists.tsv+objects.tsv+scene.glb",
			tag_id, room_id, resolved_room, label, pass,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_obj_in_room_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequirePerceptionNode(const char *action, s32 node_count,
	s32 needs_objects)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.condition.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	if (needs_objects && !s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing objects.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerLookingAtObject(
	struct chrdata *basechr, s32 chrnum, s32 tag_id, s32 label)
{
	struct defaultobj *obj;
	struct chrdata *chr;
	s32 pass = 0;

	if (!s_aiGraphRequirePerceptionNode("if_player_looking_at_object",
			s_ActiveScenarioGraphs.level_ai_if_player_looking_at_object_node_count,
			1)) {
		return s_ActiveScenarioGraphs.level_graph_active ? 1 : 0;
	}
	obj = objFindByTagId(tag_id);
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER &&
			obj && obj->prop) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		pass = g_Vars.currentplayer->lookingatprop.prop == obj->prop;
		setCurrentPlayerNum(prevplayernum);
	}
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_player_looking_at_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_looking_at_object chr=%d tag=%d label=%d result=%d source=%s objects=%s backend=graph.ai.condition.perception+ai/ailists.tsv+objects.tsv+scene.glb",
			chrnum, tag_id, label, pass,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_condition_if_player_looking_at_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTargetIsPlayer(struct chrdata *chr,
	s32 label)
{
	struct prop *target;
	s32 pass;

	if (!s_aiGraphRequirePerceptionNode("if_target_is_player",
			s_ActiveScenarioGraphs.level_ai_if_target_is_player_node_count,
			0)) {
		return s_ActiveScenarioGraphs.level_graph_active ? 1 : 0;
	}
	target = chrGetTargetProp(chr);
	pass = target && (target->type == PROPTYPE_EYESPY ||
		target->type == PROPTYPE_PLAYER);
	if (pass) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_target_is_player_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_target_is_player label=%d result=%d source=%s backend=graph.ai.condition.perception+ai/ailists.tsv",
			label, pass, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_target_is_player_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrKill(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr;
	s32 applied;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_kill_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_kill",
			"missing scenario.ai.action.chr_kill node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_kill",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	applied = chr != NULL;
	if (chr) {
		chr->actiontype = ACT_DEAD;
		chr->act_dead.fadetimer60 = -1;
		chr->act_dead.fadenow = false;
		chr->act_dead.fadewheninvis = false;
		chr->act_dead.invistimer60 = 0;
		chr->act_dead.notifychrindex = 0;
		chr->sleep = 0;
		chr->chrflags |= CHRCFLAG_KEEPCORPSEKO |
			CHRCFLAG_PERIMDISABLEDTMP;
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_chr_kill_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_kill chr=%d applied=%d source=%s backend=graph.ai.action.character_inventory+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_kill_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRemoveWeaponFromInventory(s32 weaponnum)
{
	const char *weapon_id;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_remove_weapon_from_inventory_node_count != 1) {
		return s_aiGraphRuntimeFailure("remove_weapon_from_inventory",
			"missing scenario.ai.action.remove_weapon_from_inventory node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("remove_weapon_from_inventory",
			"missing ai/ailists.tsv source");
	}
	weapon_id = catalogWeaponIdByRuntimeWeaponNum(weaponnum);
	invRemoveItemByNum(weaponnum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_remove_weapon_from_inventory_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action remove_weapon_from_inventory weapon=%s source=%s backend=graph.ai.action.character_inventory+ai/ailists.tsv",
			weapon_id ? weapon_id : "<unmapped>",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_remove_weapon_from_inventory_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteClearInventory(void)
{
	u32 prevplayernum;
	s32 playernum;
	s32 cleared = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_clear_inventory_node_count != 1) {
		return s_aiGraphRuntimeFailure("clear_inventory",
			"missing scenario.ai.action.clear_inventory node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("clear_inventory",
			"missing ai/ailists.tsv source");
	}
	prevplayernum = g_Vars.currentplayernum;
	for (playernum = 0; playernum < PLAYERCOUNT(); playernum++) {
		setCurrentPlayerNum(playernum);
		if (g_Vars.currentplayer == g_Vars.bond ||
				g_Vars.currentplayer == g_Vars.coop) {
			invClear();
#if VERSION >= VERSION_NTSC_1_0
			g_Vars.currentplayer->devicesactive = 0;
#endif
			invGiveSingleWeapon(WEAPON_UNARMED);
			bgunEquipWeapon(WEAPON_UNARMED);
			cleared++;
		}
	}
	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_clear_inventory_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action clear_inventory players=%d source=%s backend=graph.ai.action.character_inventory+ai/ailists.tsv",
			cleared, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_clear_inventory_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteReleaseObject(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_release_object_node_count != 1) {
		return s_aiGraphRuntimeFailure("release_object",
			"missing scenario.ai.action.release_object node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("release_object",
			"missing ai/ailists.tsv source");
	}
	bmoveSetModeForAllPlayers(MOVEMODE_WALK);
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_release_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action release_object source=%s backend=graph.ai.action.character_inventory+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_release_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrGrabObject(struct chrdata *basechr,
	s32 chrnum, s32 tag_id)
{
	struct defaultobj *obj;
	struct chrdata *chr;
	s32 grabbed = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_grab_object_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_grab_object",
			"missing scenario.ai.action.chr_grab_object node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("chr_grab_object",
			"missing ai/ailists.tsv or objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER &&
			obj && obj->prop) {
		u32 prevplayernum = g_Vars.currentplayernum;
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
		if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK &&
				bmoveGetCrouchPos() == CROUCHPOS_STAND &&
				g_Vars.currentplayer->crouchoffset == 0) {
			bmoveGrabProp(obj->prop);
			grabbed = 1;
		}
		setCurrentPlayerNum(prevplayernum);
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_grab_object_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_grab_object chr=%d tag=%d grabbed=%d source=%s objects=%s backend=graph.ai.action.character_inventory+ai/ailists.tsv+objects.tsv",
			chrnum, tag_id, grabbed, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_chr_grab_object_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteToggleP1P2(struct chrdata *basechr,
	s32 chrnum)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_toggle_p1p2_node_count != 1) {
		return s_aiGraphRuntimeFailure("toggle_p1p2",
			"missing scenario.ai.action.toggle_p1p2 node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("toggle_p1p2",
			"missing ai/ailists.tsv source");
	}
	if (g_Vars.coopplayernum >= 0) {
		chr = chrFindById(basechr, chrnum);
		if (chr) {
			if (chr->p1p2 == g_Vars.bondplayernum &&
					!g_Vars.coop->isdead) {
				chr->p1p2 = g_Vars.coopplayernum;
				applied = 1;
			} else if (!g_Vars.bond->isdead) {
				chr->p1p2 = g_Vars.bondplayernum;
				applied = 1;
			}
		}
	}
	g_Vars.aioffset += 3;
	if (!s_ActiveScenarioGraphs.ai_action_toggle_p1p2_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action toggle_p1p2 chr=%d applied=%d source=%s backend=graph.ai.action.player_state+ai/ailists.tsv",
			chrnum, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_toggle_p1p2_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetP1P2(struct chrdata *basechr,
	s32 chrnum, s32 target_chrnum)
{
	struct chrdata *chr1;
	struct chrdata *chr2;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_p1p2_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_p1p2",
			"missing scenario.ai.action.chr_set_p1p2 node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_p1p2",
			"missing ai/ailists.tsv source");
	}
	if (g_Vars.coopplayernum >= 0) {
		chr1 = chrFindById(basechr, chrnum);
		chr2 = chrFindById(basechr, target_chrnum);
		if (chr1 && chr2 && chr2->prop &&
				chr2->prop->type == PROPTYPE_PLAYER) {
			u32 playernum = playermgrGetPlayerNumByProp(chr2->prop);
			if (!g_Vars.players[playernum]->isdead) {
				if (chr2->prop == g_Vars.coop->prop) {
					chr1->p1p2 = g_Vars.coopplayernum;
				} else {
					chr1->p1p2 = g_Vars.bondplayernum;
				}
				applied = 1;
			}
		}
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_p1p2_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_p1p2 chr=%d target_chr=%d applied=%d source=%s backend=graph.ai.action.player_state+ai/ailists.tsv",
			chrnum, target_chrnum, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_p1p2_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetCloaked(struct chrdata *basechr,
	s32 chrnum, s32 cloaked, s32 timer)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_chr_set_cloaked_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_cloaked",
			"missing scenario.ai.action.chr_set_cloaked node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_cloaked",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && !chrIsDead(chr)) {
		if (cloaked) {
			chrCloak(chr, timer);
		} else {
			chrUncloak(chr, timer);
		}
		applied = 1;
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs.ai_action_chr_set_cloaked_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_cloaked chr=%d cloaked=%d timer=%d applied=%d source=%s backend=graph.ai.action.player_state+ai/ailists.tsv",
			chrnum, cloaked != 0, timer, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_chr_set_cloaked_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetAutogunTargetTeam(s32 tag_id, s32 team)
{
	struct defaultobj *obj;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_set_autogun_target_team_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_autogun_target_team",
			"missing scenario.ai.action.set_autogun_target_team node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure("set_autogun_target_team",
			"missing ai/ailists.tsv or objects.tsv source");
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->type == OBJTYPE_AUTOGUN) {
		struct autogunobj *autogun = (struct autogunobj *)obj;
		autogun->targetteam = team;
		autogun->target = NULL;
		applied = 1;
	}
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_set_autogun_target_team_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_autogun_target_team tag=%d team=%d applied=%d source=%s objects=%s backend=graph.ai.action.player_state+ai/ailists.tsv+objects.tsv",
			tag_id, team, applied, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs
			.ai_action_set_autogun_target_team_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireCharacterConditionNode(const char *condition,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.condition.%s node", condition);
		return s_aiGraphRuntimeFailure(condition, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(condition,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphRequireMissionGlobalConditionNode(const char *condition,
	s32 node_count)
{
	char reason[128];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.condition.%s node", condition);
		return s_aiGraphRuntimeFailure(condition, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(condition,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static void s_aiGraphApplyBranch(s32 branch_taken, s32 label,
	s32 false_offset)
{
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += false_offset;
	}
}

s32 scenarioSourceAiGraphExecuteIfIdle(struct chrdata *chr, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_idle",
			s_ActiveScenarioGraphs.level_ai_if_idle_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->actiontype == ACT_ANIM;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_idle_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_idle actiontype=%d label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			chr ? chr->actiontype : -1, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_idle_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStopped(struct chrdata *chr, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_stopped",
			s_ActiveScenarioGraphs.level_ai_if_stopped_node_count)) {
		return 0;
	}
	branch_taken = chr && chrIsStopped(chr);
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_stopped_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_stopped label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_stopped_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrDead(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_dead",
			s_ActiveScenarioGraphs.level_ai_if_chr_dead_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = (!chr || !chr->prop ||
		chr->prop->type != PROPTYPE_PLAYER) &&
		(!chr || !chr->model || chrIsDead(chr));
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_dead_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_dead chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_dead_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrDeathAnimationFinished(
	struct chrdata *basechr, s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_chr_death_animation_finished",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_death_animation_finished_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (!chr || !chr->prop) {
		branch_taken = 1;
	} else if (chr->prop->type == PROPTYPE_PLAYER) {
		u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
		branch_taken = g_Vars.players[playernum]->isdead;
	} else {
		branch_taken = chr->actiontype == ACT_DEAD;
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_death_animation_finished_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_death_animation_finished chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_death_animation_finished_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrKnockedOut(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_knocked_out",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_knocked_out_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = (!chr || !chr->prop ||
		chr->prop->type != PROPTYPE_PLAYER) &&
		(!chr || !chr->model || chr->actiontype == ACT_DRUGGEDKO ||
			chr->actiontype == ACT_DRUGGEDDROP ||
			chr->actiontype == ACT_DRUGGEDCOMINGUP);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_chr_knocked_out_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_knocked_out chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_chr_knocked_out_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCanSeeTarget(struct chrdata *chr,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_can_see_target",
			s_ActiveScenarioGraphs
				.level_ai_if_can_see_target_node_count)) {
		return 0;
	}
	branch_taken = chr && chrCheckCanSeeTarget(chr);
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs.ai_condition_if_can_see_target_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_can_see_target label=%d branch=%d source=%s backend=graph.ai.condition.lifecycle+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_can_see_target_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumArghsLessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_num_arghs_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_num_arghs_less_than_node_count)) {
		return 0;
	}
	current = chrGetNumArghs(chr);
	branch_taken = current < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_arghs_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_arghs_less_than threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_arghs_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumArghsGreaterThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_num_arghs_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_num_arghs_greater_than_node_count)) {
		return 0;
	}
	current = chrGetNumArghs(chr);
	branch_taken = current > threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_arghs_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_arghs_greater_than threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_arghs_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumCloseArghsLessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_num_close_arghs_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_num_close_arghs_less_than_node_count)) {
		return 0;
	}
	current = chrGetNumCloseArghs(chr);
	branch_taken = current < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_close_arghs_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_close_arghs_less_than threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_close_arghs_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumCloseArghsGreaterThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_num_close_arghs_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_num_close_arghs_greater_than_node_count)) {
		return 0;
	}
	current = chrGetNumCloseArghs(chr);
	branch_taken = current > threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_close_arghs_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_close_arghs_greater_than threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_close_arghs_greater_than_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphChrHealthComparison(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 greater_than, f32 *current_out)
{
	struct chrdata *chr = chrFindById(basechr, chrnum);
	f32 current = 0.0f;
	s32 pass = 0;

	if (chr && chr->prop) {
		if (chr->prop->type == PROPTYPE_PLAYER) {
			u32 playernum = playermgrGetPlayerNumByProp(chr->prop);
			current = g_Vars.players[playernum]->bondhealth * 8.0f;
		} else {
			current = chr->maxdamage - chr->damage;
		}
		pass = greater_than ? value > current : value < current;
	}
	if (current_out) {
		*current_out = current;
	}
	return pass;
}

s32 scenarioSourceAiGraphExecuteIfChrHealthGreaterThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label)
{
	f32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_health_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_health_greater_than_node_count)) {
		return 0;
	}
	branch_taken = s_aiGraphChrHealthComparison(basechr, chrnum, value, 1,
		&current);
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_health_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_health_greater_than chr=%d value=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, (double)value, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_health_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrHealthLessThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label)
{
	f32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_health_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_health_less_than_node_count)) {
		return 0;
	}
	branch_taken = s_aiGraphChrHealthComparison(basechr, chrnum, value, 0,
		&current);
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_health_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_health_less_than chr=%d value=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, (double)value, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_health_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrShieldLessThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label)
{
	struct chrdata *chr;
	f32 current = 0.0f;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_shield_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_shield_less_than_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		current = chrGetShield(chr);
	}
	branch_taken = chr && current < value;
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_shield_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_shield_less_than chr=%d value=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, (double)value, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_shield_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrShieldGreaterThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label)
{
	struct chrdata *chr;
	f32 current = 0.0f;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_chr_shield_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_shield_greater_than_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		current = chrGetShield(chr);
	}
	branch_taken = chr && current > value;
	s_aiGraphApplyBranch(branch_taken, label, 6);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_shield_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_shield_greater_than chr=%d value=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, (double)value, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_shield_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfInjured(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_injured",
			s_ActiveScenarioGraphs.level_ai_if_injured_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = chr && (chr->chrflags & CHRCFLAG_JUST_INJURED);
	if (branch_taken) {
		chr->chrflags &= ~CHRCFLAG_JUST_INJURED;
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs.ai_condition_if_injured_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_injured chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_injured_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfShieldDamaged(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_shield_damaged",
			s_ActiveScenarioGraphs
				.level_ai_if_shield_damaged_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	branch_taken = chr && (chr->chrflags & CHRCFLAG_SHIELDDAMAGED);
	if (branch_taken) {
		chr->chrflags &= ~CHRCFLAG_SHIELDDAMAGED;
	}
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_shield_damaged_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_shield_damaged chr=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_shield_damaged_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfMoraleLessThan(struct chrdata *chr,
	s32 threshold, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_morale_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_morale_less_than_node_count)) {
		return 0;
	}
	current = chr ? chr->morale : 0;
	branch_taken = chr && current < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_morale_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_morale_less_than threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_morale_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfMoraleLessThanRandom(struct chrdata *chr,
	s32 label)
{
	s32 morale;
	s32 random;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_morale_less_than_random",
			s_ActiveScenarioGraphs
				.level_ai_if_morale_less_than_random_node_count)) {
		return 0;
	}
	morale = chr ? chr->morale : 0;
	random = chr ? chr->random : 0;
	branch_taken = chr && morale < random;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_morale_less_than_random_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_morale_less_than_random morale=%d random=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			morale, random, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_morale_less_than_random_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfAlertness(struct chrdata *chr,
	s32 threshold, s32 comparison_mode, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode("if_alertness",
			s_ActiveScenarioGraphs.level_ai_if_alertness_node_count)) {
		return 0;
	}
	current = chr ? chr->alertness : 0;
	branch_taken = chr && ((current < threshold && comparison_mode == 0) ||
		(threshold < current && comparison_mode == 1));
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs.ai_condition_if_alertness_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_alertness threshold=%d mode=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			threshold, comparison_mode, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_alertness_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrAlertnessLessThan(
	struct chrdata *basechr, s32 threshold, s32 chrnum, s32 label)
{
	struct chrdata *chr;
	s32 current = 0;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_chr_alertness_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_chr_alertness_less_than_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		current = chr->alertness;
	}
	branch_taken = chr && current < threshold;
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_alertness_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_alertness_less_than chr=%d threshold=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			chrnum, threshold, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_alertness_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfAlertnessLessThanRandom(struct chrdata *chr,
	s32 label)
{
	s32 alertness;
	s32 random;
	s32 branch_taken;

	if (!s_aiGraphRequireCharacterConditionNode(
			"if_alertness_less_than_random",
			s_ActiveScenarioGraphs
				.level_ai_if_alertness_less_than_random_node_count)) {
		return 0;
	}
	alertness = chr ? chr->alertness : 0;
	random = chr ? chr->random : 0;
	branch_taken = chr && alertness < random;
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_alertness_less_than_random_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_alertness_less_than_random alertness=%d random=%d label=%d branch=%d source=%s backend=graph.ai.condition.character_state+ai/ailists.tsv",
			alertness, random, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_alertness_less_than_random_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjectiveComplete(s32 objective_index,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode("if_objective_complete",
			s_ActiveScenarioGraphs
				.level_ai_if_objective_complete_node_count)) {
		return 0;
	}
	branch_taken = objective_index < objectiveGetCount() &&
		objectiveCheck(objective_index) == OBJECTIVE_COMPLETE &&
		(objectiveGetDifficultyBits(objective_index) &
			(1 << lvGetDifficulty()));
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_objective_complete_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_objective_complete objective=%d label=%d branch=%d source=%s mission=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			objective_index, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.mission_graph_path[0]
				? s_ActiveScenarioGraphs.mission_graph_path : "(inactive)");
		s_ActiveScenarioGraphs
			.ai_condition_if_objective_complete_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjectiveFailed(s32 objective_index,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode("if_objective_failed",
			s_ActiveScenarioGraphs
				.level_ai_if_objective_failed_node_count)) {
		return 0;
	}
	branch_taken = objective_index < objectiveGetCount() &&
		objectiveCheck(objective_index) == OBJECTIVE_FAILED &&
		(objectiveGetDifficultyBits(objective_index) &
			(1 << lvGetDifficulty()));
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_objective_failed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_objective_failed objective=%d label=%d branch=%d source=%s mission=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			objective_index, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.mission_graph_path[0]
				? s_ActiveScenarioGraphs.mission_graph_path : "(inactive)");
		s_ActiveScenarioGraphs.ai_condition_if_objective_failed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfAllObjectivesComplete(s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_all_objectives_complete",
			s_ActiveScenarioGraphs
				.level_ai_if_all_objectives_complete_node_count)) {
		return 0;
	}
	branch_taken = objectiveIsAllComplete();
	s_aiGraphApplyBranch(branch_taken, label, 3);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_all_objectives_complete_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_all_objectives_complete label=%d branch=%d source=%s mission=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.mission_graph_path[0]
				? s_ActiveScenarioGraphs.mission_graph_path : "(inactive)");
		s_ActiveScenarioGraphs
			.ai_condition_if_all_objectives_complete_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDifficultyLessThan(s32 difficulty,
	s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_difficulty_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_difficulty_less_than_node_count)) {
		return 0;
	}
	current = lvGetDifficulty();
	branch_taken = current < difficulty;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_difficulty_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_difficulty_less_than difficulty=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			difficulty, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_difficulty_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfDifficultyGreaterThan(s32 difficulty,
	s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_difficulty_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_difficulty_greater_than_node_count)) {
		return 0;
	}
	current = lvGetDifficulty();
	branch_taken = current > difficulty;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_difficulty_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_difficulty_greater_than difficulty=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			difficulty, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_difficulty_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStageTimerLessThan(f32 seconds, s32 label)
{
	f32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_stage_timer_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_stage_timer_less_than_node_count)) {
		return 0;
	}
	current = lvGetStageTimeInSeconds();
	branch_taken = current < seconds;
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_stage_timer_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_stage_timer_less_than seconds=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			(double)seconds, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_stage_timer_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStageTimerGreaterThan(f32 seconds,
	s32 label)
{
	f32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_stage_timer_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_stage_timer_greater_than_node_count)) {
		return 0;
	}
	current = lvGetStageTimeInSeconds();
	branch_taken = current > seconds;
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_stage_timer_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_stage_timer_greater_than seconds=%.3f current=%.3f label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			(double)seconds, (double)current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_stage_timer_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStageIdLessThan(s32 stagenum, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_stage_id_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_stage_id_less_than_node_count)) {
		return 0;
	}
	current = mainGetStageNum();
	branch_taken = stagenum > current;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_stage_id_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_stage_id_less_than stagenum=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			stagenum, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_stage_id_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfStageIdGreaterThan(s32 stagenum, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_stage_id_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_stage_id_greater_than_node_count)) {
		return 0;
	}
	current = mainGetStageNum();
	branch_taken = current > stagenum;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_stage_id_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_stage_id_greater_than stagenum=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			stagenum, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_stage_id_greater_than_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireQuadrantPresetNode(const char *action,
	s32 node_count)
{
	char reason[96];

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	if (!s_ActiveScenarioGraphs.pads_path[0] ||
			!s_ActiveScenarioGraphs.waypoints_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing pads.tsv or navigation/waypoints.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant(struct chrdata *chr,
	s32 quadrant, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireQuadrantPresetNode(
			"condition.if_waypoint_within_quadrant",
			s_ActiveScenarioGraphs
				.level_ai_if_waypoint_within_quadrant_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->prop &&
		((quadrant != QUADRANT_TOWARDSTARGET &&
				quadrant != QUADRANT_AWAYFROMTARGET) ||
			chrGetTargetProp(chr)) &&
		func0f04a4ec(chr, (u8)quadrant);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_waypoint_within_quadrant_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_waypoint_within_quadrant quadrant=%d label=%d result=%d source=%s pads=%s waypoints=%s backend=graph.ai.action.quadrant_preset+ai/ailists.tsv+pads.tsv+navigation.generate",
			quadrant, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path,
			s_ActiveScenarioGraphs.waypoints_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_waypoint_within_quadrant_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant(
	struct chrdata *chr, s32 quadrant, s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireQuadrantPresetNode(
			"action.set_pad_preset_to_target_quadrant",
			s_ActiveScenarioGraphs
				.level_ai_set_pad_preset_to_target_quadrant_node_count)) {
		return 0;
	}
	branch_taken = chr && chr->prop && chrGetTargetProp(chr) &&
		chrSetPadPresetToWaypointWithinTargetQuadrant(chr,
			(u8)quadrant);
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_target_quadrant_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_pad_preset_to_target_quadrant quadrant=%d label=%d result=%d source=%s pads=%s waypoints=%s backend=graph.ai.action.quadrant_preset+ai/ailists.tsv+pads.tsv+navigation.generate",
			quadrant, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path,
			s_ActiveScenarioGraphs.waypoints_path);
		s_ActiveScenarioGraphs
			.ai_action_set_pad_preset_to_target_quadrant_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumPlayersLessThan(s32 player_count,
	s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_num_players_less_than",
			s_ActiveScenarioGraphs
				.level_ai_if_num_players_less_than_node_count)) {
		return 0;
	}
	current = PLAYERCOUNT();
	branch_taken = player_count > current;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_players_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_players_less_than player_count=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			player_count, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_players_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfKillCountGreaterThan(s32 kill_count,
	s32 label)
{
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_kill_count_greater_than",
			s_ActiveScenarioGraphs
				.level_ai_if_kill_count_greater_than_node_count)) {
		return 0;
	}
	branch_taken = g_Vars.killcount > kill_count;
	s_aiGraphApplyBranch(branch_taken, label, 4);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_kill_count_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_kill_count_greater_than kill_count=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			kill_count, g_Vars.killcount, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_kill_count_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfNumKnockedOutChrs(s32 count,
	s32 comparison_mode, s32 label)
{
	s32 current;
	s32 branch_taken;

	if (!s_aiGraphRequireMissionGlobalConditionNode(
			"if_num_knocked_out_chrs",
			s_ActiveScenarioGraphs
				.level_ai_if_num_knocked_out_chrs_node_count)) {
		return 0;
	}
	current = mpstatsGetTotalKnockoutCount();
	branch_taken = (count < current && comparison_mode == 0) ||
		(current < count && comparison_mode == 1);
	s_aiGraphApplyBranch(branch_taken, label, 5);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_num_knocked_out_chrs_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_num_knocked_out_chrs count=%d mode=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json",
			count, comparison_mode, current, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_num_knocked_out_chrs_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteKillBond(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_kill_bond_node_count != 1) {
		return s_aiGraphRuntimeFailure("kill_bond",
			"missing scenario.ai.action.kill_bond node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("kill_bond",
			"missing ai/ailists.tsv source");
	}
	if (!s_ActiveScenarioGraphs.mission_graph_path[0]) {
		return s_aiGraphRuntimeFailure("kill_bond",
			"missing mission.graph.json source");
	}
	if (!g_Vars.bond) {
		return s_aiGraphRuntimeFailure("kill_bond",
			"missing Bond player state");
	}
	g_Vars.bond->isdead = true;
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_kill_bond_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action kill_bond applied=1 source=%s mission=%s backend=graph.ai.action.mission_global+ai/ailists.tsv+mission.graph.json",
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.mission_graph_path);
		s_ActiveScenarioGraphs.ai_action_kill_bond_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPouncebitsEq(struct chrdata *chr,
	s32 pouncebits, s32 label)
{
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_pouncebits_eq_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_pouncebits_eq",
			"missing scenario.ai.condition.if_pouncebits_eq node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_pouncebits_eq",
			"missing ai/ailists.tsv source");
	}
	branch_taken = chr && chr->pouncebits == pouncebits;
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_pouncebits_eq_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_pouncebits_eq value=%d current=%d label=%d branch=%d source=%s backend=graph.ai.condition.state_device+ai/ailists.tsv",
			pouncebits, chr ? chr->pouncebits : -1, label,
			branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_pouncebits_eq_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTrainingPcHolographed(s32 label)
{
	struct trainingdata *data;
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_if_training_pc_holographed_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_training_pc_holographed",
			"missing scenario.ai.condition.if_training_pc_holographed node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_training_pc_holographed",
			"missing ai/ailists.tsv source");
	}
	data = dtGetData();
	branch_taken = data && data->holographedpc;
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_training_pc_holographed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_training_pc_holographed label=%d branch=%d source=%s backend=graph.ai.condition.state_device+ai/ailists.tsv",
			label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_training_pc_holographed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfPlayerUsingDevice(struct chrdata *basechr,
	s32 chrnum, s32 devicenum, s32 label)
{
	struct chrdata *chr;
	struct prop *prop;
	u32 prevplayernum;
	s32 playernum = -1;
	s32 branch_taken = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_player_using_device_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_player_using_device",
			"missing scenario.ai.condition.if_player_using_device node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_player_using_device",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	prop = chr ? chr->prop : NULL;
	prevplayernum = g_Vars.currentplayernum;
	if (prop && prop->type == PROPTYPE_PLAYER) {
		playernum = playermgrGetPlayerNumByProp(prop);
		setCurrentPlayerNum(playernum);
		branch_taken = currentPlayerGetDeviceState(devicenum) ==
			DEVICESTATE_ACTIVE;
		setCurrentPlayerNum(prevplayernum);
	}
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_player_using_device_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_player_using_device chr=%d player=%d device=%d label=%d branch=%d source=%s backend=graph.ai.condition.state_device+ai/ailists.tsv",
			chrnum, playernum, devicenum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_player_using_device_logged = 1;
	}
	return 1;
}

static void s_aiGraphPlayTeleportSound(s32 soundnum)
{
	struct sndstate *handle;
	f32 pitch = 0.4f;
#if VERSION >= VERSION_NTSC_1_0
	s32 mainpri;
	s32 audiopri;

	mainpri = osGetThreadPri(0);
	audiopri = osGetThreadPri(&g_AudioManager.thread);
	osSetThreadPri(0, audiopri + 1);
#endif
	handle = sndStart(var80095200, soundnum, NULL, -1, -1, -1, -1, -1);
	if (handle) {
		audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(u32 *)&pitch);
	}
#if VERSION >= VERSION_NTSC_1_0
	osSetThreadPri(0, mainpri);
#endif
}

s32 scenarioSourceAiGraphExecuteChrBeginOrEndTeleport(struct chrdata *basechr,
	s32 chrnum, s32 pad_id)
{
	struct chrdata *chr;
	u32 prevplayernum;
	s32 playernum = -1;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_chr_begin_or_end_teleport_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_begin_or_end_teleport",
			"missing scenario.ai.action.chr_begin_or_end_teleport node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_begin_or_end_teleport",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	prevplayernum = g_Vars.currentplayernum;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
	}
	if (pad_id == 0) {
		g_Vars.currentplayer->teleportstate = TELEPORTSTATE_EXITING;
		g_Vars.currentplayer->teleporttime = 0;
	} else {
		g_Vars.currentplayer->teleporttime = 0;
		g_Vars.currentplayer->teleportstate = TELEPORTSTATE_PREENTER;
		g_Vars.currentplayer->teleportpad = pad_id;
		g_Vars.currentplayer->teleportcamerapad = 0;
		s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT);
	}
	g_Vars.aioffset += 5;
	setCurrentPlayerNum(prevplayernum);
	if (!s_ActiveScenarioGraphs
			.ai_action_chr_begin_or_end_teleport_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_begin_or_end_teleport chr=%d player=%d pad=%d source=%s backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.tsv",
			chrnum, playernum, pad_id,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_chr_begin_or_end_teleport_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrTeleportFullWhite(struct chrdata *basechr,
	s32 chrnum, s32 label)
{
	struct chrdata *chr;
	u32 prevplayernum;
	s32 playernum = -1;
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_if_chr_teleport_full_white_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_chr_teleport_full_white",
			"missing scenario.ai.condition.if_chr_teleport_full_white node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_chr_teleport_full_white",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	prevplayernum = g_Vars.currentplayernum;
	if (chr && chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		playernum = playermgrGetPlayerNumByProp(chr->prop);
		setCurrentPlayerNum(playernum);
	}
	if (g_Vars.currentplayer->teleportstate < TELEPORTSTATE_WHITE) {
		g_Vars.aioffset += 4;
		branch_taken = 0;
	} else {
		s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN);
		g_Vars.currentplayer->teleportstate = TELEPORTSTATE_WHITE;
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
		branch_taken = 1;
	}
	setCurrentPlayerNum(prevplayernum);
	if (!s_ActiveScenarioGraphs
			.ai_condition_if_chr_teleport_full_white_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_teleport_full_white chr=%d player=%d label=%d branch=%d source=%s backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.tsv",
			chrnum, playernum, label, branch_taken,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_condition_if_chr_teleport_full_white_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetCutsceneWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum, s32 fallback_weaponnum)
{
	struct chrdata *chr;
	s32 model_id;
	s32 fallback_model_id;
	s32 applied = 0;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_chr_set_cutscene_weapon_node_count != 1) {
		return s_aiGraphRuntimeFailure("chr_set_cutscene_weapon",
			"missing scenario.ai.action.chr_set_cutscene_weapon node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("chr_set_cutscene_weapon",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	model_id = playermgrGetModelOfWeapon(weaponnum);
	fallback_model_id = playermgrGetModelOfWeapon(fallback_weaponnum);
	if (chr) {
		if (weaponnum == 0xff) {
			if (fallback_weaponnum == 0xff) {
				if (chr->weapons_held[0]) {
					struct weaponobj *weapon =
						chr->weapons_held[0]->weapon;
					bool valid = true;

					switch (weapon->weaponnum) {
					case WEAPON_FALCON2:
					case WEAPON_FALCON2_SILENCER:
					case WEAPON_FALCON2_SCOPE:
					case WEAPON_MAGSEC4:
					case WEAPON_MAULER:
					case WEAPON_PHOENIX:
					case WEAPON_DY357MAGNUM:
					case WEAPON_DY357LX:
					case WEAPON_CMP150:
						valid = false;
					}

					if (valid) {
						weaponDeleteFromChr(chr, HAND_LEFT);
						weaponDeleteFromChr(chr, HAND_RIGHT);
						applied = 1;
					}
				}
			} else if (chr->weapons_held[0] == NULL &&
					chr->weapons_held[1] == NULL &&
					fallback_model_id >= 0) {
				weaponCreateForChr(chr, fallback_model_id,
					fallback_weaponnum, 0, NULL, NULL);
				applied = 1;
			}
		} else {
			weaponDeleteFromChr(chr, HAND_LEFT);
			weaponDeleteFromChr(chr, HAND_RIGHT);
			applied = 1;

			if (model_id >= 0) {
				weaponCreateForChr(chr, model_id, weaponnum,
					0, NULL, NULL);
			}
			if (fallback_model_id >= 0) {
				weaponCreateForChr(chr, fallback_model_id,
					fallback_weaponnum, OBJFLAG_WEAPON_LEFTHANDED,
					NULL, NULL);
			}
		}
	}
	g_Vars.aioffset += 5;
	if (!s_ActiveScenarioGraphs
			.ai_action_chr_set_cutscene_weapon_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_cutscene_weapon chr=%d weapon=%d fallback_weapon=%d applied=%d source=%s backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.tsv",
			chrnum, weaponnum, fallback_weaponnum, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_chr_set_cutscene_weapon_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireCutscenePresentationNode(const char *action,
	s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteFadeScreen(u32 color, s32 num_frames)
{
	if (!s_aiGraphRequireCutscenePresentationNode("fade_screen",
			s_ActiveScenarioGraphs.level_ai_fade_screen_node_count)) {
		return 0;
	}
	lvConfigureFade(color, num_frames);
	if (!s_ActiveScenarioGraphs.ai_action_fade_screen_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action fade_screen color=0x%08x frames=%d source=%s backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv",
			color, num_frames, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_fade_screen_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfFadeComplete(s32 label)
{
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_fade_complete_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_fade_complete",
			"missing scenario.ai.condition.if_fade_complete node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_fade_complete",
			"missing ai/ailists.tsv source");
	}
	branch_taken = lvIsFadeActive() == false;
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_condition_if_fade_complete_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_fade_complete label=%d branch=%d source=%s backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv",
			label, branch_taken, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_condition_if_fade_complete_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrHudpieceVisible(struct chrdata *basechr,
	s32 chrnum, s32 visible)
{
	struct chrdata *chr;
	s32 applied = 0;

	if (!s_aiGraphRequireCutscenePresentationNode("set_chr_hudpiece_visible",
			s_ActiveScenarioGraphs
				.level_ai_set_chr_hudpiece_visible_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->prop && chr->model) {
		chrSetHudpieceVisible(chr, visible != 0);
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_chr_hudpiece_visible_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_hudpiece_visible chr=%d visible=%d applied=%d source=%s backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv",
			chrnum, visible != 0, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_hudpiece_visible_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPassiveMode(s32 enable)
{
	if (!s_aiGraphRequireCutscenePresentationNode("set_passive_mode",
			s_ActiveScenarioGraphs.level_ai_set_passive_mode_node_count)) {
		return 0;
	}
	bgunSetPassiveMode(enable != 0);
	if (!s_ActiveScenarioGraphs.ai_action_set_passive_mode_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_passive_mode enable=%d source=%s backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv",
			enable != 0, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_passive_mode_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrSetFiringInCutscene(struct chrdata *basechr,
	s32 chrnum, s32 firing)
{
	struct chrdata *chr;
	struct coord from = {0, 0, 0};
	struct coord to = {0, 0, 0};
	s32 applied = 0;

	if (!s_aiGraphRequireCutscenePresentationNode(
			"chr_set_firing_in_cutscene",
			s_ActiveScenarioGraphs
				.level_ai_chr_set_firing_in_cutscene_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr && chr->weapons_held[HAND_RIGHT]) {
		if (firing) {
			chrSetFiring(chr, HAND_RIGHT, true);
			chrUpdateFireslot(chr, HAND_RIGHT, true, false, &from, &to);
		} else {
			chrSetFiring(chr, HAND_RIGHT, false);
		}
		applied = 1;
	}
	if (!s_ActiveScenarioGraphs
			.ai_action_chr_set_firing_in_cutscene_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_set_firing_in_cutscene chr=%d firing=%d applied=%d source=%s backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv",
			chrnum, firing != 0, applied,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs
			.ai_action_chr_set_firing_in_cutscene_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetPortalFlag(s32 portalnum, s32 flags)
{
	if (!s_aiGraphRequireCutscenePresentationNode("set_portal_flag",
			s_ActiveScenarioGraphs.level_ai_set_portal_flag_node_count)) {
		return 0;
	}
	g_BgPortals[portalnum].flags |= flags;
	if (!s_ActiveScenarioGraphs.ai_action_set_portal_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_portal_flag portal=%d flags=0x%02x source=%s scene=scene.glb backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv+scene.glb",
			portalnum, flags & 0xff, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_portal_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty(s32 label)
{
	static bool waited = false;
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_if_music_event_queue_is_empty_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_music_event_queue_is_empty",
			"missing scenario.ai.condition.if_music_event_queue_is_empty node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_music_event_queue_is_empty",
			"missing ai/ailists.tsv source");
	}
	if (g_MusicEventQueueLength && !waited) {
		waited = true;
		g_Vars.aioffset += 4;
		branch_taken = 0;
	} else {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
		waited = false;
		branch_taken = 1;
	}
	if (!s_ActiveScenarioGraphs
			.ai_action_if_music_event_queue_is_empty_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_music_event_queue_is_empty queue=%d waited=%d branch=%d label=%d source=%s backend=graph.ai.condition.music_mode+ai/ailists.tsv",
			g_MusicEventQueueLength, waited, branch_taken, label,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_music_event_queue_is_empty_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCoopMode(s32 label)
{
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_coop_mode_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_coop_mode",
			"missing scenario.ai.condition.if_coop_mode node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_coop_mode",
			"missing ai/ailists.tsv source");
	}
	branch_taken = g_Vars.normmplayerisrunning == false &&
		g_MissionConfig.iscoop;
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_coop_mode_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_coop_mode coop=%d normmp=%d branch=%d label=%d source=%s backend=graph.ai.condition.music_mode+ai/ailists.tsv",
			g_MissionConfig.iscoop, g_Vars.normmplayerisrunning,
			branch_taken, label, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_coop_mode_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan(
	struct chrdata *basechr, s32 chrnum, f32 distance, s32 padnum,
	s32 label)
{
	struct chrdata *chr;
	f32 actual;
	s32 branch_taken;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_if_chr_same_floor_distance_to_pad_less_than_node_count != 1) {
		return s_aiGraphRuntimeFailure(
			"if_chr_same_floor_distance_to_pad_less_than",
			"missing scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.pads_path[0]) {
		return s_aiGraphRuntimeFailure(
			"if_chr_same_floor_distance_to_pad_less_than",
			"missing ai/ailists.tsv or pads.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	actual = chr ? chrGetSameFloorDistanceToPad(chr, padnum & 0xffffffff)
		: -1.0f;
	branch_taken = chr && actual < distance;
	if (branch_taken) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 8;
	}
	if (!s_ActiveScenarioGraphs
			.ai_action_if_chr_same_floor_distance_to_pad_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI condition if_chr_same_floor_distance_to_pad_less_than chr=%d pad=%d actual=%.2f threshold=%.2f branch=%d label=%d source=%s pads=%s backend=graph.ai.action.pad_reference+ai/ailists.tsv+pads.tsv",
			chrnum, padnum, (double)actual, (double)distance,
			branch_taken, label, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.pads_path);
		s_ActiveScenarioGraphs
			.ai_action_if_chr_same_floor_distance_to_pad_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRemoveReferencesToChr(struct chrdata *chr)
{
	s32 applied;
	s32 prop_index;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs
			.level_ai_remove_references_to_chr_node_count != 1) {
		return s_aiGraphRuntimeFailure("remove_references_to_chr",
			"missing scenario.ai.action.remove_references_to_chr node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("remove_references_to_chr",
			"missing ai/ailists.tsv source");
	}
	applied = chr && chr->prop;
	prop_index = -1;
	if (applied) {
		prop_index = (s32)(chr->prop - g_Vars.props);
		chrClearReferences(prop_index);
	}
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_remove_references_to_chr_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action remove_references_to_chr prop=%d applied=%d source=%s backend=graph.ai.action.pad_reference+ai/ailists.tsv",
			prop_index, applied, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_remove_references_to_chr_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireModelPartNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv or objects.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteChrToggleModelPart(struct chrdata *basechr,
	s32 chrnum, s32 partnum)
{
	struct chrdata *chr;

	if (!s_aiGraphRequireModelPartNode("chr_toggle_model_part",
			s_ActiveScenarioGraphs.level_ai_chr_toggle_model_part_node_count)) {
		return 0;
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		chrToggleModelPart(chr, partnum);
	}
	if (!s_ActiveScenarioGraphs.ai_action_chr_toggle_model_part_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action chr_toggle_model_part chr=%d part=%d source=%s objects=%s backend=graph.ai.action.model_part+ai/ailists.tsv+objects.tsv",
			chrnum, partnum, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_chr_toggle_model_part_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteObjSetModelPartVisible(s32 tag_id,
	s32 partnum, s32 visible)
{
	struct defaultobj *obj;

	if (!s_aiGraphRequireModelPartNode("obj_set_model_part_visible",
			s_ActiveScenarioGraphs.level_ai_obj_set_model_part_visible_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		objSetModelPartVisible(obj, partnum, visible != 0);
	}
	if (!s_ActiveScenarioGraphs.ai_action_obj_set_model_part_visible_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action obj_set_model_part_visible tag=%d part=%d visible=%d source=%s objects=%s backend=graph.ai.action.model_part+ai/ailists.tsv+objects.tsv",
			tag_id, partnum, visible != 0,
			s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_obj_set_model_part_visible_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireObjectHealthNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0] ||
			!s_ActiveScenarioGraphs.objects_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv or objects.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfObjHealthLessThan(s32 tag_id,
	s32 damage, s32 label)
{
	struct defaultobj *obj;
	s32 result = 0;

	if (!s_aiGraphRequireObjectHealthNode("if_obj_health_less_than",
			s_ActiveScenarioGraphs.level_ai_if_obj_health_less_than_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop && obj->damage < damage) {
		result = 1;
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 6;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_obj_health_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_obj_health_less_than tag=%d damage=%d result=%d source=%s objects=%s backend=graph.ai.action.object_health+ai/ailists.tsv+objects.tsv",
			tag_id, damage, result, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_if_obj_health_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetObjHealth(s32 tag_id, s32 damage)
{
	struct defaultobj *obj;

	if (!s_aiGraphRequireObjectHealthNode("set_obj_health",
			s_ActiveScenarioGraphs.level_ai_set_obj_health_node_count)) {
		return 0;
	}
	obj = objFindByTagId(tag_id);
	if (obj && obj->prop) {
		obj->damage = damage;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_obj_health_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_obj_health tag=%d damage=%d source=%s objects=%s backend=graph.ai.action.object_health+ai/ailists.tsv+objects.tsv",
			tag_id, damage, s_ActiveScenarioGraphs.ai_lists_path,
			s_ActiveScenarioGraphs.objects_path);
		s_ActiveScenarioGraphs.ai_action_set_obj_health_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation(
	struct chrdata *basechr, s32 chrnum, s32 animation)
{
	struct chrdata *chr;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_chr_special_death_animation_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_chr_special_death_animation",
			"missing scenario.ai.action.set_chr_special_death_animation node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_chr_special_death_animation",
			"missing ai/ailists.tsv source");
	}
	chr = chrFindById(basechr, chrnum);
	if (chr) {
		chr->specialdie = animation;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_chr_special_death_animation_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_chr_special_death_animation chr=%d animation=%d source=%s backend=graph.ai.action.special_death+ai/ailists.tsv",
			chrnum, animation, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_chr_special_death_animation_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetRoomToSearch(struct chrdata *chr)
{
	struct chrdata *target;
	s32 room = -1;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_room_to_search_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_room_to_search",
			"missing scenario.ai.action.set_room_to_search node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_room_to_search",
			"missing ai/ailists.tsv source");
	}
	target = chrFindById(chr, CHR_TARGET);
	if (chr && target && target->prop) {
		room = target->prop->rooms[0];
		chr->roomtosearch = room;
	}
	if (!s_ActiveScenarioGraphs.ai_action_set_room_to_search_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_room_to_search room=%d source=%s scene=scene.glb backend=graph.ai.action.room_search+ai/ailists.tsv+scene.glb",
			room, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_room_to_search_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteRestartTimer(struct chrdata *chr,
	struct chopperobj *hovercar)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_restart_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("restart_timer",
			"missing scenario.ai.action.restart_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("restart_timer",
			"missing ai/ailists.tsv source");
	}
	if (chr) {
		chrRestartTimer(chr);
	} else if (hovercar) {
		chopperRestartTimer(hovercar);
	}
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_restart_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action restart_timer target=%s source=%s backend=graph.ai.action.restart_timer+ai/ailists.tsv",
			chr ? "chr" : (hovercar ? "hovercar" : "none"),
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_restart_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteResetTimer(struct chrdata *chr)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_reset_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("reset_timer",
			"missing scenario.ai.action.reset_timer node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("reset_timer", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("reset_timer",
			"missing ai/ailists.tsv source");
	}
	chr->timer60 = 0;
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_reset_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action reset_timer source=%s backend=graph.ai.action.reset_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_reset_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecutePauseTimer(struct chrdata *chr)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_pause_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("pause_timer",
			"missing scenario.ai.action.pause_timer node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("pause_timer", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("pause_timer",
			"missing ai/ailists.tsv source");
	}
	chr->hidden &= ~CHRHFLAG_TIMER_RUNNING;
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_pause_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action pause_timer source=%s backend=graph.ai.action.pause_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_pause_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteResumeTimer(struct chrdata *chr)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_resume_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("resume_timer",
			"missing scenario.ai.action.resume_timer node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("resume_timer", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("resume_timer",
			"missing ai/ailists.tsv source");
	}
	chr->hidden |= CHRHFLAG_TIMER_RUNNING;
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_resume_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action resume_timer source=%s backend=graph.ai.action.resume_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_resume_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTimerStopped(struct chrdata *chr, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_timer_stopped_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_timer_stopped",
			"missing scenario.ai.action.if_timer_stopped node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("if_timer_stopped", "missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_timer_stopped",
			"missing ai/ailists.tsv source");
	}
	result = (chr->hidden & CHRHFLAG_TIMER_RUNNING) == 0;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_timer_stopped_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_timer_stopped result=%d source=%s backend=graph.ai.action.if_timer_stopped+ai/ailists.tsv",
			result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_timer_stopped_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTimerGreaterThanRandom(struct chrdata *chr,
	s32 label)
{
	f32 timer;
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_random_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_timer_greater_than_random",
			"missing scenario.ai.action.if_timer_greater_than_random node");
	}
	if (!chr) {
		return s_aiGraphRuntimeFailure("if_timer_greater_than_random",
			"missing chr");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_timer_greater_than_random",
			"missing ai/ailists.tsv source");
	}
	timer = chrGetTimer(chr);
	result = chr->random < timer;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_timer_greater_than_random_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_timer_greater_than_random timer=%f random=%u result=%d source=%s backend=graph.ai.action.if_timer_greater_than_random+ai/ailists.tsv",
			timer, chr->random, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_timer_greater_than_random_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTimerLessThan(struct chrdata *chr,
	struct chopperobj *hovercar, f32 value, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_timer_less_than_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_timer_less_than",
			"missing scenario.ai.action.if_timer_less_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_timer_less_than",
			"missing ai/ailists.tsv source");
	}
	result = (chr && chrGetTimer(chr) < value) ||
		(hovercar && chopperGetTimer(hovercar) < value);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 6;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_timer_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_timer_less_than value=%f result=%d source=%s backend=graph.ai.action.if_timer_less_than+ai/ailists.tsv",
			value, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_timer_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfTimerGreaterThan(struct chrdata *chr,
	struct chopperobj *hovercar, f32 value, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_timer_greater_than_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_timer_greater_than",
			"missing scenario.ai.action.if_timer_greater_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_timer_greater_than",
			"missing ai/ailists.tsv source");
	}
	if (chr) {
		chrGetTimer(chr);
	}
	if (hovercar) {
		chopperGetTimer(hovercar);
	}
	result = (chr && chrGetTimer(chr) > value) ||
		(hovercar && chopperGetTimer(hovercar) > value);
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 6;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_timer_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_timer_greater_than value=%f result=%d source=%s backend=graph.ai.action.if_timer_greater_than+ai/ailists.tsv",
			value, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_timer_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteShowCountdownTimer(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_show_countdown_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("show_countdown_timer",
			"missing scenario.ai.action.show_countdown_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("show_countdown_timer",
			"missing ai/ailists.tsv source");
	}
	countdownTimerSetVisible(COUNTDOWNTIMERREASON_AI, true);
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_show_countdown_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action show_countdown_timer source=%s backend=graph.ai.action.show_countdown_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_show_countdown_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteHideCountdownTimer(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_hide_countdown_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("hide_countdown_timer",
			"missing scenario.ai.action.hide_countdown_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("hide_countdown_timer",
			"missing ai/ailists.tsv source");
	}
	countdownTimerSetVisible(COUNTDOWNTIMERREASON_AI, false);
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_hide_countdown_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action hide_countdown_timer source=%s backend=graph.ai.action.hide_countdown_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_hide_countdown_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetCountdownTimerValue(f32 seconds)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_countdown_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_countdown_timer",
			"missing scenario.ai.action.set_countdown_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_countdown_timer",
			"missing ai/ailists.tsv source");
	}
	countdownTimerSetValue60(seconds * 60);
	g_Vars.aioffset += 4;
	if (!s_ActiveScenarioGraphs.ai_action_set_countdown_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_countdown_timer seconds=%f source=%s backend=graph.ai.action.set_countdown_timer+ai/ailists.tsv",
			seconds, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_countdown_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStopCountdownTimer(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_stop_countdown_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("stop_countdown_timer",
			"missing scenario.ai.action.stop_countdown_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("stop_countdown_timer",
			"missing ai/ailists.tsv source");
	}
	countdownTimerSetRunning(false);
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_stop_countdown_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action stop_countdown_timer source=%s backend=graph.ai.action.stop_countdown_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_stop_countdown_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteStartCountdownTimer(void)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_start_countdown_timer_node_count != 1) {
		return s_aiGraphRuntimeFailure("start_countdown_timer",
			"missing scenario.ai.action.start_countdown_timer node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("start_countdown_timer",
			"missing ai/ailists.tsv source");
	}
	countdownTimerSetRunning(true);
	g_Vars.aioffset += 2;
	if (!s_ActiveScenarioGraphs.ai_action_start_countdown_timer_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action start_countdown_timer source=%s backend=graph.ai.action.start_countdown_timer+ai/ailists.tsv",
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_start_countdown_timer_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCountdownTimerStopped(s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_countdown_timer_stopped_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_stopped",
			"missing scenario.ai.action.if_countdown_timer_stopped node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_stopped",
			"missing ai/ailists.tsv source");
	}
	result = !countdownTimerIsRunning();
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 3;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_countdown_timer_stopped_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_countdown_timer_stopped result=%d source=%s backend=graph.ai.action.if_countdown_timer_stopped+ai/ailists.tsv",
			result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_countdown_timer_stopped_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCountdownTimerLessThan(f32 seconds,
	s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_countdown_timer_less_than_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_less_than",
			"missing scenario.ai.action.if_countdown_timer_less_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_less_than",
			"missing ai/ailists.tsv source");
	}
	result = countdownTimerGetValue60() < seconds * 60;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_countdown_timer_less_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_countdown_timer_less_than seconds=%f result=%d source=%s backend=graph.ai.action.if_countdown_timer_less_than+ai/ailists.tsv",
			seconds, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_countdown_timer_less_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfCountdownTimerGreaterThan(f32 seconds,
	s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_countdown_timer_greater_than_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_greater_than",
			"missing scenario.ai.action.if_countdown_timer_greater_than node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_countdown_timer_greater_than",
			"missing ai/ailists.tsv source");
	}
	result = countdownTimerGetValue60() > seconds * 60;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 5;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_countdown_timer_greater_than_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_countdown_timer_greater_than seconds=%f result=%d source=%s backend=graph.ai.action.if_countdown_timer_greater_than+ai/ailists.tsv",
			seconds, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_countdown_timer_greater_than_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetSavefileFlag(u32 flag)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_set_savefile_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("set_savefile_flag",
			"missing scenario.ai.action.set_savefile_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("set_savefile_flag",
			"missing ai/ailists.tsv source");
	}
	gamefileSetFlag(flag);
	if (!s_ActiveScenarioGraphs.ai_action_set_savefile_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_savefile_flag flag=0x%02x source=%s backend=graph.ai.action.set_savefile_flag+ai/ailists.tsv",
			flag, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_savefile_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteUnsetSavefileFlag(u32 flag)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_unset_savefile_flag_node_count != 1) {
		return s_aiGraphRuntimeFailure("unset_savefile_flag",
			"missing scenario.ai.action.unset_savefile_flag node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("unset_savefile_flag",
			"missing ai/ailists.tsv source");
	}
	gamefileUnsetFlag(flag);
	if (!s_ActiveScenarioGraphs.ai_action_unset_savefile_flag_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action unset_savefile_flag flag=0x%02x source=%s backend=graph.ai.action.unset_savefile_flag+ai/ailists.tsv",
			flag, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_unset_savefile_flag_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSavefileFlagIsSet(u32 flag, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_savefile_flag_set_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_savefile_flag_set",
			"missing scenario.ai.action.if_savefile_flag_set node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_savefile_flag_set",
			"missing ai/ailists.tsv source");
	}
	result = gamefileHasFlag(flag) ? 1 : 0;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_savefile_flag_set_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_savefile_flag_set flag=0x%02x result=%d source=%s backend=graph.ai.action.if_savefile_flag_set+ai/ailists.tsv",
			flag, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_savefile_flag_set_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset(u32 flag, s32 label)
{
	s32 result;

	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (s_ActiveScenarioGraphs.level_ai_if_savefile_flag_unset_node_count != 1) {
		return s_aiGraphRuntimeFailure("if_savefile_flag_unset",
			"missing scenario.ai.action.if_savefile_flag_unset node");
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure("if_savefile_flag_unset",
			"missing ai/ailists.tsv source");
	}
	result = gamefileHasFlag(flag) ? 0 : 1;
	if (result) {
		g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,
			g_Vars.aioffset, label);
	} else {
		g_Vars.aioffset += 4;
	}
	if (!s_ActiveScenarioGraphs.ai_action_if_savefile_flag_unset_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action if_savefile_flag_unset flag=0x%02x result=%d source=%s backend=graph.ai.action.if_savefile_flag_unset+ai/ailists.tsv",
			flag, result, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_if_savefile_flag_unset_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireHudNode(const char *action, s32 node_count)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	return 1;
}

static s32 s_aiGraphHudPlayerForChr(struct chrdata *basechr, s32 chrnum)
{
	struct chrdata *chr = chrFindById(basechr, chrnum);

	if (chr && chr->prop && (chr->prop->type & 0xff) == PROPTYPE_PLAYER) {
		return playermgrGetPlayerNumByProp(chr->prop);
	}
	return g_Vars.currentplayernum;
}

s32 scenarioSourceAiGraphExecuteShowHudmsg(struct chrdata *basechr,
	s32 chrnum, u16 text_id)
{
	s32 prevplayernum;
	s32 playernum;
	char *text;

	if (!s_aiGraphRequireHudNode("show_hudmsg",
			s_ActiveScenarioGraphs.level_ai_show_hudmsg_node_count)) {
		return 0;
	}

	text = langGet(text_id);
	prevplayernum = g_Vars.currentplayernum;
	playernum = s_aiGraphHudPlayerForChr(basechr, chrnum);
	setCurrentPlayerNum(playernum);
	hudmsgCreate(text, HUDMSGTYPE_DEFAULT);
	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 5;

	if (!s_ActiveScenarioGraphs.ai_action_show_hudmsg_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action show_hudmsg chr=%d text=%u player=%d source=%s backend=graph.ai.action.hud+ai/ailists.tsv",
			chrnum, (unsigned)text_id, playernum,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_show_hudmsg_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteShowHudmsgMiddle(s32 mode, s32 colour,
	u16 text_id)
{
	char *text;

	if (!s_aiGraphRequireHudNode("show_hudmsg_middle",
			s_ActiveScenarioGraphs.level_ai_show_hudmsg_middle_node_count)) {
		return 0;
	}

	if (mode == 0) {
		text = langGet(text_id);
		hudmsgCreateWithColour(text, HUDMSGTYPE_7, colour);
	} else if (mode == 1) {
		text = langGet(text_id);
		hudmsgCreateWithColour(text, HUDMSGTYPE_8, colour);
	} else {
		hudmsgRemoveAll();
	}
	g_Vars.aioffset += 6;

	if (!s_ActiveScenarioGraphs.ai_action_show_hudmsg_middle_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action show_hudmsg_middle mode=%d colour=%d text=%u source=%s backend=graph.ai.action.hud+ai/ailists.tsv",
			mode, colour, (unsigned)text_id,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_show_hudmsg_middle_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteShowHudmsgTopMiddle(struct chrdata *basechr,
	s32 chrnum, u16 text_id, s32 colour)
{
	s32 prevplayernum;
	s32 playernum;
	char *text;

	if (!s_aiGraphRequireHudNode("show_hudmsg_top_middle",
			s_ActiveScenarioGraphs.level_ai_show_hudmsg_top_middle_node_count)) {
		return 0;
	}

	text = langGet(text_id);
	prevplayernum = g_Vars.currentplayernum;
	playernum = s_aiGraphHudPlayerForChr(basechr, chrnum);
	setCurrentPlayerNum(playernum);
	hudmsgCreateWithColour(text, HUDMSGTYPE_INGAMESUBTITLE, colour);
	setCurrentPlayerNum(prevplayernum);
	g_Vars.aioffset += 6;

	if (!s_ActiveScenarioGraphs.ai_action_show_hudmsg_top_middle_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action show_hudmsg_top_middle chr=%d text=%u colour=%d player=%d source=%s backend=graph.ai.action.hud+ai/ailists.tsv",
			chrnum, (unsigned)text_id, colour, playernum,
			s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_show_hudmsg_top_middle_logged = 1;
	}
	return 1;
}

static s32 s_aiGraphRequireVehicleNode(const char *action, s32 node_count,
	s32 require_paths)
{
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (node_count != 1) {
		char reason[96];
		snprintf(reason, sizeof(reason),
			"missing scenario.ai.action.%s node", action);
		return s_aiGraphRuntimeFailure(action, reason);
	}
	if (!s_ActiveScenarioGraphs.ai_lists_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing ai/ailists.tsv source");
	}
	if (require_paths && !s_ActiveScenarioGraphs.paths_path[0]) {
		return s_aiGraphRuntimeFailure(action,
			"missing navigation/paths.tsv source");
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteHovercarBeginPath(s32 path_id)
{
	struct path *path;

	if (!s_aiGraphRequireVehicleNode("hovercar_begin_path",
			s_ActiveScenarioGraphs.level_ai_hovercar_begin_path_node_count,
			1)) {
		return 0;
	}

	path = pathFindById(path_id);
	if (!path && (g_Vars.truck || g_Vars.hovercar)) {
		g_Vars.aioffset += 3;
		return s_aiGraphRuntimeFailure("hovercar_begin_path",
			"missing navigation path id");
	}
	if (g_Vars.truck) {
		g_Vars.truck->path = path;
		g_Vars.truck->nextstep = 0;
	}
	if (g_Vars.hovercar) {
		struct chopperobj *chopper = chopperFromHovercar(g_Vars.hovercar);
		g_Vars.hovercar->path = path;
		g_Vars.hovercar->nextstep = 0;
		g_Vars.hovercar->path->flags |= PATHFLAG_INUSE;

		if (chopper) {
			chopper->targetvisible = false;
			chopper->attackmode = CHOPPERMODE_PATROL;
			chopper->turnrot60 = 0;
			chopper->roty = 0;
			chopper->rotx = 0;
			chopper->gunroty = 0;
			chopper->gunrotx = 0;
			chopper->barrelrot = 0;
			chopper->barrelrotspeed = 0;
			chopper->vz = 0;
			chopper->vy = 0;
			chopper->vx = 0;
			chopper->otz = 0;
			chopper->oty = 0;
			chopper->otx = 0;
			chopper->power = 0;
			chopper->bob = 0;
			chopper->bobstrength = 0.05f;
			chopper->timer60 = 0;
			chopper->patroltimer60 = 0;
			chopper->cw = 0;
			chopper->weaponsarmed = true;
			chopper->base.flags |= OBJFLAG_CHOPPER_INIT;
		} else {
			g_Vars.hovercar->weaponsarmed = false;
		}
	}
	g_Vars.aioffset += 3;

	if (!s_ActiveScenarioGraphs.ai_action_hovercar_begin_path_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action hovercar_begin_path path=%d found=%d source=%s backend=graph.ai.action.vehicle+navigation/paths.tsv",
			path_id, path ? 1 : 0, s_ActiveScenarioGraphs.paths_path);
		s_ActiveScenarioGraphs.ai_action_hovercar_begin_path_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetVehicleSpeed(f32 speedaim, f32 speedtime)
{
	if (!s_aiGraphRequireVehicleNode("set_vehicle_speed",
			s_ActiveScenarioGraphs.level_ai_set_vehicle_speed_node_count,
			0)) {
		return 0;
	}

	if (g_Vars.truck) {
		g_Vars.truck->speedaim = speedaim;
		g_Vars.truck->speedtime60 = speedtime;
	}
	if (g_Vars.hovercar) {
		g_Vars.hovercar->speedaim = speedaim;
		g_Vars.hovercar->speedtime60 = speedtime;
	}
	g_Vars.aioffset += 6;

	if (!s_ActiveScenarioGraphs.ai_action_set_vehicle_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_vehicle_speed speedaim=%.3f speedtime=%.3f source=%s backend=graph.ai.action.vehicle+ai/ailists.tsv",
			speedaim, speedtime, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_vehicle_speed_logged = 1;
	}
	return 1;
}

s32 scenarioSourceAiGraphExecuteSetRotorSpeed(f32 speedaim, f32 speedtime)
{
	if (!s_aiGraphRequireVehicleNode("set_rotor_speed",
			s_ActiveScenarioGraphs.level_ai_set_rotor_speed_node_count,
			0)) {
		return 0;
	}

	if (g_Vars.heli) {
		g_Vars.heli->rotoryspeedaim = speedaim;
		g_Vars.heli->rotoryspeedtime = speedtime;
	}
	g_Vars.aioffset += 6;

	if (!s_ActiveScenarioGraphs.ai_action_set_rotor_speed_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: AI action set_rotor_speed speedaim=%.3f speedtime=%.3f source=%s backend=graph.ai.action.vehicle+ai/ailists.tsv",
			speedaim, speedtime, s_ActiveScenarioGraphs.ai_lists_path);
		s_ActiveScenarioGraphs.ai_action_set_rotor_speed_logged = 1;
	}
	return 1;
}

static s32 s_missionObjectiveGraphRuntimeFailure(const char *action,
	s32 index, const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
		sysFatalError("ASSET.SOURCE_ONLY: mission graph '%s' cannot %s "
			"runtime objective index %d from public source (%s); "
			"refusing legacy-only objective behavior.",
			s_ActiveScenarioGraphs.mission_id[0]
				? s_ActiveScenarioGraphs.mission_id : "?",
			action && action[0] ? action : "validate",
			index,
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"MISSION.GRAPH: cannot %s runtime objective index %d for '%s' (%s)",
		action && action[0] ? action : "validate",
		index,
		s_ActiveScenarioGraphs.mission_id[0]
			? s_ActiveScenarioGraphs.mission_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 0;
}

s32 scenarioSourceObjectiveGraphReportRuntimeMismatch(s32 index,
	const char *action, const char *reason)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 0;
	}
	return s_missionObjectiveGraphRuntimeFailure(action, index, reason);
}

s32 scenarioSourceMissionGraphRecordPhase(const char *phase,
	const char *reason)
{
	if (!s_ActiveScenarioGraphs.mission_graph_active) {
		return 1;
	}
	if (!phase || !phase[0]) {
		return s_missionObjectiveGraphRuntimeFailure("phase", -1,
			"missing mission phase");
	}
	if (s_ActiveScenarioGraphs.mission_phase_node_count < 5) {
		return s_missionObjectiveGraphRuntimeFailure("phase", -1,
			"missing mission phase source nodes");
	}
	if (strcmp(s_ActiveScenarioGraphs.mission_phase_state, phase) == 0) {
		return 1;
	}

	s_copyString(s_ActiveScenarioGraphs.mission_phase_state,
		sizeof(s_ActiveScenarioGraphs.mission_phase_state), phase);
	sysLogPrintf(LOG_NOTE,
		"MISSION.GRAPH: phase transition from graph source '%s' phase=%s reason=%s backend=graph.mission.phase",
		s_ActiveScenarioGraphs.mission_graph_path,
		phase,
		reason && reason[0] ? reason : "runtime");
	return 1;
}

s32 scenarioSourceObjectiveGraphGetCriterionCount(s32 index)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return -1;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return -1;
	}
	return s_ActiveScenarioGraphs.mission_objectives[index].criteria_count;
}

s32 scenarioSourceObjectiveGraphGetCriterionType(s32 index,
	s32 criterion_index, u8 *out_type)
{
	const scenario_source_objective_node_t *node;
	const scenario_source_objective_criteria_t *criteria;
	s32 criteria_offset;

	if (out_type) {
		*out_type = 0;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active ||
			!out_type) {
		return 0;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives
			|| !s_ActiveScenarioGraphs.mission_objective_criteria) {
		return 0;
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (criterion_index < 0 || criterion_index >= node->criteria_count) {
		return 0;
	}

	criteria_offset = node->criteria_start + criterion_index;
	if (criteria_offset < 0 ||
			criteria_offset >=
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes) {
		return 0;
	}

	criteria =
		&s_ActiveScenarioGraphs.mission_objective_criteria[
			criteria_offset];
	*out_type = criteria->type;
	return 1;
}

s32 scenarioSourceObjectiveGraphGetCriterionOperand(s32 index,
	s32 criterion_index, scenario_source_objective_operand_t *out_operand)
{
	const scenario_source_objective_node_t *node;
	const scenario_source_objective_criteria_t *criteria;
	s32 criteria_offset;

	if (out_operand) {
		memset(out_operand, 0, sizeof(*out_operand));
		out_operand->tag_id = -1;
		out_operand->pad = -1;
		out_operand->initial_status = -1;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active ||
			!out_operand) {
		return 0;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives
			|| !s_ActiveScenarioGraphs.mission_objective_criteria) {
		return 0;
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (criterion_index < 0 || criterion_index >= node->criteria_count) {
		return 0;
	}

	criteria_offset = node->criteria_start + criterion_index;
	if (criteria_offset < 0 ||
			criteria_offset >=
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes) {
		return 0;
	}

	criteria =
		&s_ActiveScenarioGraphs.mission_objective_criteria[
			criteria_offset];
	out_operand->type = criteria->type;
	out_operand->stage_flag_mask = criteria->stage_flag_mask;
	out_operand->tag_id = criteria->tag_id;
	out_operand->pad = criteria->pad;
	out_operand->match_value = criteria->match_value;
	out_operand->initial_status = criteria->initial_status;
	out_operand->runtime_status = criteria->runtime_status;
	out_operand->runtime_status_valid = criteria->runtime_status_valid;
	out_operand->runtime_object_state_valid =
		criteria->runtime_object_state_valid;
	out_operand->runtime_object_present =
		criteria->runtime_object_present;
	out_operand->runtime_object_healthy =
		criteria->runtime_object_healthy;
	out_operand->runtime_object_held_by_player =
		criteria->runtime_object_held_by_player;
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordCriterionStatus(const void *criteria,
	s32 status)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (!criteria ||
			!s_ActiveScenarioGraphs.mission_objective_criteria) {
		return s_missionObjectiveGraphRuntimeFailure("state", -1,
			"missing objective criteria pointer");
	}

	for (i = 0; i < s_ActiveScenarioGraphs
			.mission_objective_criteria_nodes; i++) {
		scenario_source_objective_criteria_t *entry =
			&s_ActiveScenarioGraphs.mission_objective_criteria[i];

		if (entry->runtime_criteria == criteria) {
			entry->runtime_status = status;
			entry->runtime_status_valid = 1;

			if (!s_ActiveScenarioGraphs
					.mission_objective_state_logged) {
				sysLogPrintf(LOG_NOTE,
					"MISSION.GRAPH: objective state updated from graph source '%s' objective=%d node=%s status=%d backend=graph.objective.state",
					s_ActiveScenarioGraphs.mission_objectives_path,
					entry->objective_index,
					entry->graph_node,
					status);
				s_ActiveScenarioGraphs
					.mission_objective_state_logged = 1;
			}

			return 1;
		}
	}

	return s_missionObjectiveGraphRuntimeFailure("state", -1,
		"objective criteria status was not bound to graph source");
}

s32 scenarioSourceObjectiveGraphRecordObjectState(s32 tag_id,
	s32 present, s32 healthy, s32 held_by_player)
{
	s32 i;
	s32 updated = 0;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (tag_id < 0 ||
			!s_ActiveScenarioGraphs.mission_objective_criteria) {
		return s_missionObjectiveGraphRuntimeFailure("object-state", -1,
			"missing objective object state source");
	}

	for (i = 0; i < s_ActiveScenarioGraphs
			.mission_objective_criteria_nodes; i++) {
		scenario_source_objective_criteria_t *entry =
			&s_ActiveScenarioGraphs.mission_objective_criteria[i];

		if (entry->tag_id == tag_id &&
				s_objectiveCriterionUsesObjectState(entry->type)) {
			entry->runtime_object_state_valid = 1;
			entry->runtime_object_present = present != 0;
			entry->runtime_object_healthy = healthy != 0;
			entry->runtime_object_held_by_player =
				held_by_player != 0;
			updated = 1;

			if (!s_ActiveScenarioGraphs
					.mission_objective_object_state_logged) {
				sysLogPrintf(LOG_NOTE,
					"MISSION.GRAPH: objective object state updated from graph source '%s' objective=%d node=%s tag=%d present=%d healthy=%d held=%d backend=graph.objective.object_state",
					s_ActiveScenarioGraphs.mission_objectives_path,
					entry->objective_index,
					entry->graph_node,
					tag_id,
					present != 0,
					healthy != 0,
					held_by_player != 0);
				s_ActiveScenarioGraphs
					.mission_objective_object_state_logged = 1;
			}
		}
	}

	(void)updated;
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordStageFlags(u32 flags)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}

	s_ActiveScenarioGraphs.mission_stage_flags = flags;
	s_ActiveScenarioGraphs.mission_stage_flags_valid = 1;

	if (!s_ActiveScenarioGraphs.mission_stage_flags_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: mission flags updated in graph runtime '%s' flags=0x%08x backend=graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			flags);
		s_ActiveScenarioGraphs.mission_stage_flags_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphHasStageFlag(u32 flag, s32 *out_has_flag)
{
	if (out_has_flag) {
		*out_has_flag = 0;
	}
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 0;
	}
	if (!s_ActiveScenarioGraphs.mission_stage_flags_valid || flag == 0) {
		return 0;
	}
	if (out_has_flag) {
		*out_has_flag =
			(s_ActiveScenarioGraphs.mission_stage_flags & flag) != 0;
	}
	return 1;
}

s32 scenarioSourceObjectiveGraphRecordEvaluate(s32 index, s32 criteria_count)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"missing objective source node");
	}
	if (!s_ActiveScenarioGraphs.mission_objectives[index].inserted) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"objective was not inserted from graph source");
	}
	if (criteria_count !=
			s_ActiveScenarioGraphs.mission_objectives[index].criteria_count) {
		return s_missionObjectiveGraphRuntimeFailure("evaluate", index,
			"criteria count differs from graph source");
	}

	if (!s_ActiveScenarioGraphs.mission_objective_evaluate_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective criteria evaluated from graph source '%s' index=%d criteria=%d backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			index, criteria_count);
		s_ActiveScenarioGraphs.mission_objective_evaluate_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphRecordInsert(const struct objective *objective)
{
	const u32 *cmd;
	const scenario_source_objective_node_t *node;
	s32 index;
	s32 seen_criteria = 0;
	s32 guard = 0;

	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return 1;
	}
	if (!objective) {
		return s_missionObjectiveGraphRuntimeFailure("insert", -1,
			"missing objective pointer");
	}

	index = objective->index;
	if (index < 0 || index >= s_ActiveScenarioGraphs.mission_objective_nodes
			|| !s_ActiveScenarioGraphs.mission_objectives) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"missing objective source node");
	}

	node = &s_ActiveScenarioGraphs.mission_objectives[index];
	if (((u32)(u8)objective->difficulties) != node->difficulty_bits) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"difficulty mask differs from graph source");
	}

	cmd = (const u32 *)objective;
	while (guard++ < 128) {
		u8 type = (u8)PD_BE32(cmd[0]);
		u32 words;

		if (type == OBJTYPE_ENDOBJECTIVE) {
			break;
		}

		words = s_setupCommandLengthBytesForType(type) / sizeof(u32);
		if (words == 0) {
			return s_missionObjectiveGraphRuntimeFailure("insert",
				index, "objective command has no source length");
		}

		if (type != OBJTYPE_BEGINOBJECTIVE) {
			scenario_source_objective_criteria_t *criteria;

			if (seen_criteria >= node->criteria_count) {
				return s_missionObjectiveGraphRuntimeFailure(
					"insert", index,
					"runtime has more criteria than graph source");
			}

			criteria = &s_ActiveScenarioGraphs
				.mission_objective_criteria[
					node->criteria_start + seen_criteria];
			if (criteria->type != type) {
				return s_missionObjectiveGraphRuntimeFailure(
					"insert", index,
					"criteria type differs from graph source");
			}
			criteria->matched = 1;
			criteria->runtime_criteria = cmd;
			if (s_objectiveCriterionUsesGraphStatus(type)) {
				if (criteria->initial_status < 0) {
					return s_missionObjectiveGraphRuntimeFailure(
						"insert", index,
						"criteria graph state has no initial status");
				}
				criteria->runtime_status =
					criteria->initial_status;
				criteria->runtime_status_valid = 1;
			}
			seen_criteria++;
		}

		cmd += words;
	}

	if (guard >= 128) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"objective command stream did not terminate");
	}
	if (seen_criteria != node->criteria_count) {
		return s_missionObjectiveGraphRuntimeFailure("insert", index,
			"runtime has fewer criteria than graph source");
	}

	s_ActiveScenarioGraphs.mission_objectives[index].inserted = 1;
	if (!s_ActiveScenarioGraphs.mission_objective_insert_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective insert matched graph source '%s' index=%d node=%s criteria=%d",
			s_ActiveScenarioGraphs.mission_objectives_path,
			index, node->graph_node, node->criteria_count);
		s_ActiveScenarioGraphs.mission_objective_insert_logged = 1;
	}

	return 1;
}

s32 scenarioSourceObjectiveGraphRecordCheck(s32 index, s32 status)
{
	if (!s_ActiveScenarioGraphs.mission_objective_runtime_active) {
		return status;
	}

	if (index < 0 ||
			index >= s_ActiveScenarioGraphs.mission_objective_nodes) {
		if (assetSourceDebugIsEnabledFor(ASSET_MISSION)) {
			sysFatalError("ASSET.SOURCE_ONLY: mission graph '%s' has no "
				"objective node for runtime objective index %d "
				"(nodes=%d); refusing legacy-only objective result.",
				s_ActiveScenarioGraphs.mission_id[0]
					? s_ActiveScenarioGraphs.mission_id : "?",
				index,
				s_ActiveScenarioGraphs.mission_objective_nodes);
		}
		if (!s_ActiveScenarioGraphs.mission_objective_range_warning_logged) {
			sysLogPrintf(LOG_WARNING,
				"MISSION.GRAPH: runtime objective index %d is outside graph objective nodes=%d for '%s'",
				index,
				s_ActiveScenarioGraphs.mission_objective_nodes,
				s_ActiveScenarioGraphs.mission_id[0]
					? s_ActiveScenarioGraphs.mission_id : "?");
			s_ActiveScenarioGraphs.mission_objective_range_warning_logged = 1;
		}
		return status;
	}
	if (s_ActiveScenarioGraphs.mission_objectives
			&& !s_ActiveScenarioGraphs.mission_objectives[index].inserted) {
		s_missionObjectiveGraphRuntimeFailure("check", index,
			"objective was not inserted from graph source");
		return status;
	}

	if (!s_ActiveScenarioGraphs.mission_objective_check_logged) {
		sysLogPrintf(LOG_NOTE,
			"MISSION.GRAPH: objective check routed through graph source '%s' nodes=%d criteria=%d backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags",
			s_ActiveScenarioGraphs.mission_objectives_path,
			s_ActiveScenarioGraphs.mission_objective_nodes,
			s_ActiveScenarioGraphs.mission_objective_criteria_nodes);
		s_ActiveScenarioGraphs.mission_objective_check_logged = 1;
	}

	return status;
}

static const scenario_source_volume_row_t *s_findLevelVolumeForPad(s32 pad)
{
	s32 i;

	if (!s_ActiveScenarioGraphs.level_volumes || pad < 0) {
		return NULL;
	}

	for (i = 0; i < s_ActiveScenarioGraphs.level_volume_count; i++) {
		const scenario_source_volume_row_t *row =
			&s_ActiveScenarioGraphs.level_volumes[i];

		if (row->padnum == pad) {
			return row;
		}
	}

	return NULL;
}

static s32 s_levelGraphVolumeRuntimeFailure(s32 pad, s32 room,
	const char *reason)
{
	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: level graph '%s' cannot bind "
			"trigger volume pad=%d room=%d from public source '%s' "
			"(%s); refusing legacy-only trigger evaluation.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			pad,
			room,
			s_ActiveScenarioGraphs.volumes_path[0]
				? s_ActiveScenarioGraphs.volumes_path : "(missing)",
			reason && reason[0] ? reason : "graph mismatch");
	}

	if (!s_ActiveScenarioGraphs.level_volume_missing_logged) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.GRAPH: cannot bind trigger volume pad=%d room=%d for '%s' (%s)",
			pad,
			room,
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			reason && reason[0] ? reason : "graph mismatch");
		s_ActiveScenarioGraphs.level_volume_missing_logged = 1;
	}
	return 0;
}

s32 scenarioSourceLevelGraphCheckPadRoom(s32 pad, s32 room,
	const char *reason, s32 *out_matches)
{
	const scenario_source_volume_row_t *row;
	s32 matched;

	if (out_matches) {
		*out_matches = 0;
	}
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 0;
	}
	if (pad < 0 || room < 0) {
		return 0;
	}

	row = s_findLevelVolumeForPad(pad);
	if (!row) {
		return s_levelGraphVolumeRuntimeFailure(pad, room,
			"runtime pad was not present in volumes.tsv");
	}
	if (row->room < 0 || strcmp(row->shape, "aabb") != 0) {
		return s_levelGraphVolumeRuntimeFailure(pad, room,
			"volume row is not executable trigger source");
	}

	matched = row->room == room;
	if (out_matches) {
		*out_matches = matched;
	}

	if (!s_ActiveScenarioGraphs.level_volume_eval_logged) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.GRAPH: trigger volume evaluated from graph source '%s' pad=%d room=%d source_room=%d matched=%d reason=%s backend=graph.trigger.volumes+objective.status",
			s_ActiveScenarioGraphs.volumes_path,
			pad,
			room,
			row->room,
			matched,
			reason && reason[0] ? reason : "runtime");
		s_ActiveScenarioGraphs.level_volume_eval_logged = 1;
	}

	return 1;
}

static s32 s_setupBehaviorLinkTargetsMatch(
	const scenario_source_setup_link_t *link, const s32 *targets,
	const s32 *aux)
{
	if (!link || !targets || !aux) {
		return 0;
	}

	return link->target[0] == targets[0] &&
		link->target[1] == targets[1] &&
		link->target[2] == targets[2] &&
		link->aux[0] == aux[0] &&
		link->aux[1] == aux[1];
}

static s32 s_setupGraphRuntimeFailure(u8 type, s32 record_index,
	const char *reason)
{
	const char *kind = s_setupBehaviorLinkKindForType(type);

	if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
		sysFatalError("ASSET.SOURCE_ONLY: level graph '%s' cannot bind "
			"setup behavior link kind=%s record=%d from public source "
			"(%s); refusing legacy-only setup behavior.",
			s_ActiveScenarioGraphs.scenario_id[0]
				? s_ActiveScenarioGraphs.scenario_id : "?",
			kind ? kind : "?",
			record_index,
			reason && reason[0] ? reason : "graph mismatch");
	}

	sysLogPrintf(LOG_WARNING,
		"SCENARIO.GRAPH: cannot bind setup behavior link kind=%s record=%d for '%s' (%s)",
		kind ? kind : "?",
		record_index,
		s_ActiveScenarioGraphs.scenario_id[0]
			? s_ActiveScenarioGraphs.scenario_id : "?",
		reason && reason[0] ? reason : "graph mismatch");
	return 0;
}

s32 scenarioSourceSetupGraphRecordBehaviorLink(u8 type, s32 record_index,
	s32 target0, s32 target1, s32 target2, s32 aux0, s32 aux1)
{
	const char *kind = s_setupBehaviorLinkKindForType(type);
	s32 targets[3];
	s32 aux[2];

	if (!kind) {
		return 1;
	}
	if (!s_ActiveScenarioGraphs.level_graph_active) {
		return 1;
	}
	if (!s_ActiveScenarioGraphs.setup_links ||
			s_ActiveScenarioGraphs.setup_link_count <= 0) {
		return s_setupGraphRuntimeFailure(type, record_index,
			"missing setup behavior link source table");
	}

	targets[0] = target0;
	targets[1] = target1;
	targets[2] = target2;
	aux[0] = aux0;
	aux[1] = aux1;

	for (s32 i = 0; i < s_ActiveScenarioGraphs.setup_link_count; i++) {
		scenario_source_setup_link_t *link =
			&s_ActiveScenarioGraphs.setup_links[i];

		if (link->type != type || link->order != record_index) {
			continue;
		}
		if (!s_setupBehaviorLinkTargetsMatch(link, targets, aux)) {
			return s_setupGraphRuntimeFailure(type, record_index,
				"runtime targets differ from setup.fields.tsv");
		}

		link->matched = 1;
		if (!s_ActiveScenarioGraphs.setup_link_logged) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.GRAPH: setup behavior link registered from graph source '%s' record=%d kind=%s backend=graph.setup.links+setup.fields.tsv",
				s_ActiveScenarioGraphs.setup_fields_path,
				record_index,
				kind);
			s_ActiveScenarioGraphs.setup_link_logged = 1;
		}
		return 1;
	}

	return s_setupGraphRuntimeFailure(type, record_index,
		"runtime link record was not present in setup.fields.tsv");
}

static s32 s_loadNavigationTables(const asset_entry_t *scenario,
                                  scenario_source_navigation_t *nav)
{
	char path[FS_MAXPATH + 1];
	char *text;
	u32 text_size;

	if (!scenario || !nav) {
		return 0;
	}

	memset(nav, 0, sizeof(*nav));

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.waypoints_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/waypoints.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseWaypointsTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.waygroups_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/waygroups.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseWaygroupsTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.covers_path, path, sizeof(path))
			|| s_scenarioMemberPath(scenario, "navigation/covers.tsv",
			path, sizeof(path))) {
		text = s_loadOptionalText(path, &text_size);
		if (text) {
			if (!s_parseCoversTsv(text, nav)) {
				sysLogPrintf(LOG_ERROR,
					"SCENARIO.SOURCE: invalid public navigation table '%s'",
					path);
				free(text);
				s_freeNavigation(nav);
				return 0;
			}
			free(text);
		}
	}

	return 1;
}

static s32 s_scenarioSetupFieldsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.setup_fields_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.setup_fields_file[0]) {
		strncpy(out, scenario->ext.scenario.setup_fields_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "setup.fields.tsv", out, out_n);
}

static s32 s_scenarioObjectsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.objects_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.objects_file[0]) {
		strncpy(out, scenario->ext.scenario.objects_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "objects.tsv", out, out_n);
}

static s32 s_scenarioSpawnsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.spawns_path, out, out_n)) {
		return 1;
	}

	if (scenario->ext.scenario.spawns_file[0]) {
		strncpy(out, scenario->ext.scenario.spawns_file, out_n - 1);
		out[out_n - 1] = '\0';
		return out[0] != '\0';
	}

	return s_scenarioMemberPath(scenario, "spawns.tsv", out, out_n);
}

static s32 s_scenarioAiListsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.ai_lists_path, out, out_n)) {
		return 1;
	}

	return s_scenarioMemberPath(scenario, "ai/ailists.tsv", out, out_n);
}

static s32 s_scenarioPathsPath(const asset_entry_t *scenario,
	char *out, size_t out_n)
{
	if (!scenario || !out || out_n == 0) {
		return 0;
	}

	out[0] = '\0';
	if (s_activeGraphPathForScenario(scenario,
			s_ActiveScenarioGraphs.paths_path, out, out_n)) {
		return 1;
	}

	return s_scenarioMemberPath(scenario, "navigation/paths.tsv",
		out, out_n);
}

u8 *scenarioSourceLoadSetupForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size)
{
	const asset_entry_t *scenario;
	char setup_path[FS_MAXPATH + 1];
	char objects_path[FS_MAXPATH + 1];
	char spawns_path[FS_MAXPATH + 1];
	char ai_lists_path[FS_MAXPATH + 1];
	char paths_path[FS_MAXPATH + 1];
	u32 text_size;
	char *setup_text;
	char *objects_text;
	char *spawns_text;
	char *ai_text;
	char *paths_text;
	scenario_source_setup_table_t table;
	scenario_source_spawn_table_t spawn_table;
	scenario_source_ai_table_t ai_table;
	scenario_source_path_table_t path_table;
	u8 *setup_data;
	const char *stageid;

	if (out_size) {
		*out_size = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !s_scenarioSetupFieldsPath(scenario, setup_path,
			sizeof(setup_path))) {
		return NULL;
	}

	text_size = 0;
	setup_text = s_loadOptionalText(setup_path, &text_size);
	if (!setup_text) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: missing public setup.fields.tsv for '%s' at %s",
				scenario->id, setup_path);
		}
		return NULL;
	}

	memset(&table, 0, sizeof(table));
	memset(&spawn_table, 0, sizeof(spawn_table));
	memset(&ai_table, 0, sizeof(ai_table));
	memset(&path_table, 0, sizeof(path_table));
	if (!s_setupParseFieldsTsv(setup_text, &table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public setup.fields.tsv for '%s': %s",
			scenario->id, table.error[0] ? table.error : "parse failed");
		free(setup_text);
		s_setupFreeTable(&table);
		return NULL;
	}
	free(setup_text);

	objects_text = NULL;
	if (s_scenarioObjectsPath(scenario, objects_path, sizeof(objects_path))) {
		objects_text = s_loadOptionalText(objects_path, &text_size);
	}
	if (objects_text) {
		if (!s_setupApplyObjectsSummary(objects_text, &table)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: invalid public objects.tsv for '%s': %s",
				scenario->id, table.error[0] ? table.error : "parse failed");
			free(objects_text);
			s_setupFreeTable(&table);
			s_freePathTable(&path_table);
			s_aiFreeTable(&ai_table);
			return NULL;
		}
		free(objects_text);
	}

	spawns_text = NULL;
	if (s_scenarioSpawnsPath(scenario, spawns_path, sizeof(spawns_path))) {
		spawns_text = s_loadOptionalText(spawns_path, &text_size);
	}
	if (!spawns_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public spawns.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadSpawnSourceRows(spawns_text, &spawn_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public spawns.tsv for '%s': %s",
			scenario->id, spawn_table.error[0] ? spawn_table.error : "parse failed");
		free(spawns_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(spawns_text);

	paths_text = NULL;
	if (s_scenarioPathsPath(scenario, paths_path, sizeof(paths_path))) {
		paths_text = s_loadOptionalText(paths_path, &text_size);
	}
	if (!paths_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public navigation/paths.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadPathSourceRows(paths_text, &path_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public navigation/paths.tsv for '%s': %s",
			scenario->id, path_table.error[0]
				? path_table.error : "parse failed");
		free(paths_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(paths_text);

	ai_text = NULL;
	if (s_scenarioAiListsPath(scenario, ai_lists_path, sizeof(ai_lists_path))) {
		ai_text = s_loadOptionalText(ai_lists_path, &text_size);
	}
	if (!ai_text) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: missing public ai/ailists.tsv for '%s'",
			scenario->id);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	if (!s_loadAiListSourceRows(ai_text, &ai_table)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: invalid public ai/ailists.tsv for '%s': %s",
			scenario->id, ai_table.error[0] ? ai_table.error : "parse failed");
		free(ai_text);
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}
	free(ai_text);

	if (table.count > 1) {
		qsort(table.records, (size_t)table.count, sizeof(table.records[0]),
			s_setupCompareRecords);
	}
	if (!s_setupCollectBehaviorLinkSource(scenario, setup_path, &table)) {
		s_setupFreeTable(&table);
		s_spawnFreeTable(&spawn_table);
		s_freePathTable(&path_table);
		s_aiFreeTable(&ai_table);
		return NULL;
	}

	setup_data = s_setupBuildStageBlock(&table, &spawn_table, &ai_table,
		&path_table, scenario->id, out_size);
	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	if (setup_data) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.SOURCE: compiled setup.fields.tsv '%s', spawns.tsv '%s', navigation/paths.tsv '%s', and ai/ailists.tsv '%s' for stage '%s' as %d setup records, %d spawns, %d paths, %d AI lists (%d bytes)",
			setup_path, spawns_path, paths_path, ai_lists_path, stageid,
			table.count, spawn_table.count, path_table.count, ai_table.count,
			out_size ? *out_size : 0);
	}

	s_setupFreeTable(&table);
	s_spawnFreeTable(&spawn_table);
	s_freePathTable(&path_table);
	s_aiFreeTable(&ai_table);
	return setup_data;
}

static u32 s_sourceTileStride(void)
{
	return (u32)offsetof(struct geotilef, vertices)
		+ 3u * (u32)sizeof(struct coord);
}

static void s_sourceTileSetBounds(struct geotilef *tile)
{
	if (!tile) {
		return;
	}

	for (s32 axis = 0; axis < 3; axis++) {
		s32 min_index = 0;
		s32 max_index = 0;
		for (s32 i = 1; i < 3; i++) {
			if (tile->vertices[i].f[axis] <
					tile->vertices[min_index].f[axis]) {
				min_index = i;
			}
			if (tile->vertices[i].f[axis] >
					tile->vertices[max_index].f[axis]) {
				max_index = i;
			}
		}
		tile->min[axis] = (u8)min_index;
		tile->max[axis] = (u8)max_index;
	}
}

u8 *scenarioSourceLoadTilesForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size, s32 *out_rooms, s32 *out_tiles)
{
	const asset_entry_t *scenario;
	struct colmesh *mesh;
	s32 max_room = 0;
	s32 source_tiles = 0;
	u32 header_size;
	u32 tile_stride = s_sourceTileStride();
	u32 total_size;
	u8 *data;
	u32 *u32data;
	u32 *rooms;
	u32 cursor;
	const char *stageid;
	const char *source_path;

	if (out_size) {
		*out_size = 0;
	}
	if (out_rooms) {
		*out_rooms = 0;
	}
	if (out_tiles) {
		*out_tiles = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0]) {
		return NULL;
	}

	if (!catalogLoadTypedAsset(ASSET_SCENARIO, scenario->id)) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: failed to activate scene source for tiles '%s'",
				scenario->id);
		}
		return NULL;
	}

	mesh = catalogGetLoadedColmesh(scenario->id);
	if (!mesh || mesh->numtris <= 0) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: no scene colmesh available for tile cache '%s'",
				scenario->id);
		}
		return NULL;
	}

	for (s32 i = 0; i < mesh->numtris; i++) {
		s32 room = mesh->tris[i].roomnum;
		if (room > 0) {
			if (room > max_room) {
				max_room = room;
			}
			source_tiles++;
		}
	}

	if (max_room <= 0 || source_tiles <= 0) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: scene source '%s' has no room-tagged triangles for tile cache",
				scenario->id);
		}
		return NULL;
	}

	header_size = (u32)(max_room + 3) * (u32)sizeof(u32);
	if (source_tiles > 0
			&& (u32)source_tiles > (0xffffffffu - header_size) / tile_stride) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: tile cache too large for '%s' tiles=%d",
			scenario->id, source_tiles);
		return NULL;
	}

	total_size = header_size + (u32)source_tiles * tile_stride;
	data = mempAlloc(total_size, MEMPOOL_STAGE);
	if (!data) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: tile cache allocation failed for '%s' bytes=%u",
			scenario->id, (unsigned)total_size);
		return NULL;
	}
	memset(data, 0, total_size);

	u32data = (u32 *)data;
	*u32data = (u32)(max_room + 1);
	rooms = u32data + 1;
	cursor = header_size;

	for (s32 room = 0; room <= max_room; room++) {
		rooms[room] = cursor;
		for (s32 i = 0; i < mesh->numtris; i++) {
			const struct meshtri *src = &mesh->tris[i];
			struct geotilef *tile;
			if (src->roomnum != room) {
				continue;
			}
			tile = (struct geotilef *)(data + cursor);
			tile->header.type = GEOTYPE_TILE_F;
			tile->header.numvertices = 3;
			tile->header.flags = src->flags;
			tile->floortype = 0;
			tile->floorcol = 0;
			tile->vertices[0] = src->v0;
			tile->vertices[1] = src->v1;
			tile->vertices[2] = src->v2;
			s_sourceTileSetBounds(tile);
			cursor += tile_stride;
		}
	}
	rooms[max_room + 1] = cursor;

	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	source_path = scenario->ext.scenario.collision_file[0]
		? scenario->ext.scenario.collision_file
		: scenario->ext.scenario.scene_file;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: compiled scene tiles '%s' for stage '%s' as %d rooms, %d tiles (%u bytes) source=%s",
		scenario->id, stageid, max_room + 1, source_tiles,
		(unsigned)cursor,
		source_path && source_path[0] ? source_path : "(none)");

	if (out_size) {
		*out_size = (s32)cursor;
	}
	if (out_rooms) {
		*out_rooms = max_room + 1;
	}
	if (out_tiles) {
		*out_tiles = source_tiles;
	}
	return data;
}

struct bgportal *scenarioSourceLoadPortalsForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp, s32 *out_count)
{
	const asset_entry_t *scenario;
	char portals_path[FS_MAXPATH + 1];
	u32 text_size;
	char *text;
	scenario_source_portal_row_t *rows;
	s32 row_count;
	size_t total_size;
	u8 *data;
	struct bgportal *portals;
	size_t cursor;
	const char *stageid;

	if (out_count) {
		*out_count = 0;
	}

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !scenario->id[0] ||
			!s_scenarioMemberPath(scenario, "portals.tsv",
				portals_path, sizeof(portals_path))) {
		return NULL;
	}

	text = s_loadOptionalText(portals_path, &text_size);
	if (!text) {
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: missing public portals.tsv for '%s' at %s",
				scenario->id, portals_path);
		}
		return NULL;
	}

	rows = s_parsePortalsTsv(text, &row_count);
	free(text);
	if (!rows) {
		free(rows);
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: no usable portal rows in public source '%s'",
				portals_path);
		}
		return NULL;
	}

	total_size = (size_t)(row_count + 1) * sizeof(struct bgportal);
	for (s32 i = 0; i < row_count; i++) {
		total_size += offsetof(struct portalvertices, vertices) +
			(size_t)rows[i].vertex_count * sizeof(struct coord);
	}
	if (total_size > 0xffffu) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: portal table too large for '%s' portals=%d bytes=%u",
			scenario->id, row_count, (unsigned)total_size);
		free(rows);
		return NULL;
	}

	data = mempAlloc(ALIGN16((u32)total_size), MEMPOOL_STAGE);
	if (!data) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: portal table allocation failed for '%s' bytes=%u",
			scenario->id, (unsigned)total_size);
		free(rows);
		return NULL;
	}
	memset(data, 0, ALIGN16((u32)total_size));
	portals = (struct bgportal *)data;
	cursor = (size_t)(row_count + 1) * sizeof(struct bgportal);

	for (s32 i = 0; i < row_count; i++) {
		struct portalvertices *verts;
		size_t block_size = offsetof(struct portalvertices, vertices) +
			(size_t)rows[i].vertex_count * sizeof(struct coord);
		portals[i].verticesoffset = (u16)cursor;
		portals[i].roomnum1 = (s16)rows[i].room1;
		portals[i].roomnum2 = (s16)rows[i].room2;
		portals[i].flags = (u8)(rows[i].flags & 0xffu);
		verts = (struct portalvertices *)(data + cursor);
		verts->count = (u8)rows[i].vertex_count;
		memcpy(verts->vertices, rows[i].vertices,
			(size_t)rows[i].vertex_count * sizeof(struct coord));
		cursor += block_size;
	}

	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: compiled portals.tsv '%s' for stage '%s' as %d portals (%u bytes)",
		portals_path, stageid, row_count, (unsigned)total_size);

	if (out_count) {
		*out_count = row_count;
	}
	free(rows);
	return portals;
}

u8 *scenarioSourceLoadPadsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size)
{
	const asset_entry_t *scenario;
	char pads_path[FS_MAXPATH + 1];
	u32 text_size;
	char *text;
	scenario_source_pad_row_t *rows;
	s32 row_count;
	scenario_source_navigation_t nav;
	u8 *padfile;
	s32 padfile_size;
	s32 waypoint_count;
	s32 waygroup_count;
	s32 cover_count;
	const char *stageid;

	if (out_size) {
		*out_size = 0;
	}
	s_clearSourceWidePadOffsets();

	scenario = s_findScenarioForStage(stage, prefer_mp);
	if (!scenario || !s_scenarioPadsPath(scenario, pads_path, sizeof(pads_path))) {
		return NULL;
	}

	text_size = 0;
	text = (char *)fsFileLoad(pads_path, &text_size);
	if (!text || text_size == 0) {
		if (text) {
			free(text);
		}
		if (assetSourceDebugIsEnabledFor(ASSET_SCENARIO)) {
			sysLogPrintf(LOG_ERROR,
				"SCENARIO.SOURCE: missing public pads.tsv for '%s' at %s",
				scenario->id, pads_path);
		}
		return NULL;
	}

	rows = s_parsePadsTsv(text, &row_count);
	free(text);

	if (!rows) {
		if (rows) {
			free(rows);
		}
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.SOURCE: no usable pad rows in public source '%s'",
			pads_path);
		return NULL;
	}

	if (!s_loadNavigationTables(scenario, &nav)) {
		if (rows) {
			free(rows);
		}
		return NULL;
	}

	padfile_size = 0;
	padfile = s_buildPadfile(rows, row_count, &nav, &padfile_size);
	waypoint_count = nav.waypoint_count;
	waygroup_count = nav.waygroup_count;
	cover_count = nav.cover_count;
	free(rows);
	s_freeNavigation(&nav);

	if (!padfile) {
		return NULL;
	}

	stageid = (stage && stage->entry && stage->entry->id[0])
		? stage->entry->id : "?";
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.SOURCE: compiled pads.tsv '%s' for stage '%s' as %d pads, %d waypoints, %d waygroups, %d covers (%d bytes)",
		pads_path, stageid, row_count,
		waypoint_count, waygroup_count, cover_count, padfile_size);

	if (out_size) {
		*out_size = padfile_size;
	}
	return padfile;
}
