/*
 * ai_command_schema.c -- the typed operands of every AI command
 * (context/designs/modding/ai-typed-operands.md, slice 1). See the header.
 *
 * The table below was derived from the native handlers and their graph
 * twins and checked against every base AI row (CORE, 2026-11-16); keep the
 * copy in tools/asset_archive_conformance.py (AI_COMMAND_SCHEMA) equal to
 * it: pd-tests compares them.
 */

#include "ai_command_schema.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const s_AiOperandKindNames[AI_OPERAND_KIND_COUNT] = {
	"unused",
	"number",
	"bool",
	"flags",
	"label",
	"chr",
	"pad",
	"object",
	"room",
	"ai_list",
	"anim",
	"sound",
	"music",
	"text",
	"team",
	"squadron",
	"objective",
	"path",
	"portal",
	"model",
	"weapon",
	"body",
	"head",
	"string",
};

static const ai_command_field_t s_AiCommandFields[] = {
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0000 go_to_next */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0001 go_to_first */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0002 label */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0005 set_list */
	{ "ai_list", 3, 2, 0, AI_OPERAND_AI_LIST }, /* 0x0005 set_list */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0006 set_return_list */
	{ "ai_list", 3, 2, 0, AI_OPERAND_AI_LIST }, /* 0x0006 set_return_list */
	{ "ai_list", 2, 2, 0, AI_OPERAND_AI_LIST }, /* 0x0007 set_shot_list */
	{ "anim", 2, 2, 0, AI_OPERAND_ANIM }, /* 0x000b chr_do_animation */
	{ "start_frame", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x000b chr_do_animation */
	{ "end_frame", 6, 2, 0, AI_OPERAND_NUMBER }, /* 0x000b chr_do_animation */
	{ "flags", 8, 1, 0, AI_OPERAND_FLAGS }, /* 0x000b chr_do_animation */
	{ "merge", 9, 1, 0, AI_OPERAND_NUMBER }, /* 0x000b chr_do_animation */
	{ "chr", 10, 1, 0, AI_OPERAND_CHR }, /* 0x000b chr_do_animation */
	{ "speed_divisor", 11, 1, 0, AI_OPERAND_NUMBER }, /* 0x000b chr_do_animation */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x000c if_idle */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x000f try_sidestep */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0010 try_jump_out */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0011 try_run_sideways */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0012 try_attack_walk */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0013 try_attack_run */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0014 try_attack_roll */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0015 try_attack_stand */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x0015 try_attack_stand */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0015 try_attack_stand */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0016 try_attack_kneel */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x0016 try_attack_kneel */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0016 try_attack_kneel */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0017 try_modify_attack */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x0017 try_modify_attack */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0017 try_modify_attack */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0018 face_entity */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x0018 face_entity */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0018 face_entity */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0019 ai_0019 */
	{ "hitpart", 3, 1, 1, AI_OPERAND_NUMBER }, /* 0x0019 ai_0019 */
	{ "weapon", 4, 1, 0, AI_OPERAND_WEAPON }, /* 0x0019 ai_0019 */
	{ "weapon_upgrade", 5, 1, 0, AI_OPERAND_FLAGS }, /* 0x0019 ai_0019 */
	{ "weapon_unk", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x0019 ai_0019 */
	{ "weapon_function", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x0019 ai_0019 */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x001a chr_damage_chr */
	{ "target_chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x001a chr_damage_chr */
	{ "hitpart", 4, 1, 1, AI_OPERAND_NUMBER }, /* 0x001a chr_damage_chr */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x001b consider_grenade_throw */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x001b consider_grenade_throw */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x001b consider_grenade_throw */
	{ "model", 2, 2, 0, AI_OPERAND_MODEL }, /* 0x001c drop_item */
	{ "weapon", 4, 1, 0, AI_OPERAND_WEAPON }, /* 0x001c drop_item */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x001c drop_item */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x001d jog_to_pad */
	{ "speed", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x001e go_to_pad_preset */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x001f walk_to_pad */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x0020 run_to_pad */
	{ "path", 2, 1, 0, AI_OPERAND_PATH }, /* 0x0021 set_path */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0023 if_patrolling */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0026 remove_chr */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x0027 try_start_alarm */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0027 try_start_alarm */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x002a try_run_from_target */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x002b try_jog_to_target_prop */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x002c try_walk_to_target_prop */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x002d try_run_to_target_prop */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x002e try_go_to_cover_prop */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x002f try_jog_to_chr */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x002f try_jog_to_chr */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0030 try_walk_to_chr */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0030 try_walk_to_chr */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0031 try_run_to_chr */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0031 try_run_to_chr */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0032 if_stopped */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0033 if_chr_dead */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0033 if_chr_dead */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0034 if_chr_death_animation_finished */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0034 if_chr_death_animation_finished */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0035 if_can_see_target */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0037 if_random_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0037 if_random_less_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0038 if_random_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0038 if_random_greater_than */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0039 if_can_hear_alarm */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x003a if_alarm_active */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x003b if_gas_active */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x003c if_hears_target */
	{ "mode", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x003d if_saw_injury */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x003d if_saw_injury */
	{ "mode", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x003e if_saw_death */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x003e if_saw_death */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x003f if_los_to_target */
	{ "distance", 2, 4, 0, AI_OPERAND_NUMBER }, /* 0x0040 if_target_nearly_in_sight */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0040 if_target_nearly_in_sight */
	{ "distance", 2, 4, 0, AI_OPERAND_NUMBER }, /* 0x0041 if_nearly_in_targets_sight */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0041 if_nearly_in_targets_sight */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0042 set_pad_preset_to_pad_on_route_to_target */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0043 if_saw_target_recently */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0044 if_heard_target_recently */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0045 if_los_to_chr */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0045 if_los_to_chr */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0046 if_never_been_on_screen */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0047 if_on_screen */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0048 if_chr_in_on_screen_room */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0048 if_chr_in_on_screen_room */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x0049 if_room_is_on_screen */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0049 if_room_is_on_screen */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x004a if_target_aiming_at_me */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x004b if_near_miss */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x004c if_sees_suspicious_item */
	{ "angle", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x004d if_target_in_fov_left */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x004d if_target_in_fov_left */
	{ "angle", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x004e if_check_fov_with_target */
	{ "use_x", 3, 1, 0, AI_OPERAND_BOOL }, /* 0x004e if_check_fov_with_target */
	{ "invert_yvisang", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x004e if_check_fov_with_target */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x004e if_check_fov_with_target */
	{ "angle", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x004f if_target_out_of_fov_left */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x004f if_target_out_of_fov_left */
	{ "angle", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0050 if_target_in_fov */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0050 if_target_in_fov */
	{ "angle", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0051 if_target_out_of_fov */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0051 if_target_out_of_fov */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0052 if_distance_to_target_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0052 if_distance_to_target_less_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0053 if_distance_to_target_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0053 if_distance_to_target_greater_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0054 if_chr_distance_to_pad_less_than */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x0054 if_chr_distance_to_pad_less_than */
	{ "pad", 5, 2, 0, AI_OPERAND_PAD }, /* 0x0054 if_chr_distance_to_pad_less_than */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x0054 if_chr_distance_to_pad_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0055 if_chr_distance_to_pad_greater_than */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x0055 if_chr_distance_to_pad_greater_than */
	{ "pad", 5, 2, 0, AI_OPERAND_PAD }, /* 0x0055 if_chr_distance_to_pad_greater_than */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x0055 if_chr_distance_to_pad_greater_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0056 if_distance_to_chr_less_than */
	{ "chr", 4, 1, 0, AI_OPERAND_CHR }, /* 0x0056 if_distance_to_chr_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0056 if_distance_to_chr_less_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0057 if_distance_to_chr_greater_than */
	{ "chr", 4, 1, 0, AI_OPERAND_CHR }, /* 0x0057 if_distance_to_chr_greater_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0057 if_distance_to_chr_greater_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0058 ai_0058 */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0058 ai_0058 */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0059 if_distance_from_target_to_pad_less_than */
	{ "pad", 4, 2, 0, AI_OPERAND_PAD }, /* 0x0059 if_distance_from_target_to_pad_less_than */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x0059 if_distance_from_target_to_pad_less_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x005a if_distance_from_target_to_pad_greater_than */
	{ "pad", 4, 2, 0, AI_OPERAND_PAD }, /* 0x005a if_distance_from_target_to_pad_greater_than */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x005a if_distance_from_target_to_pad_greater_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x005b if_chr_in_room */
	{ "mode", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x005b if_chr_in_room */
	{ "pad", 4, 2, 0, AI_OPERAND_PAD }, /* 0x005b if_chr_in_room */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x005b if_chr_in_room */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x005c if_target_in_room */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x005c if_target_in_room */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x005d if_chr_has_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x005d if_chr_has_object */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x005d if_chr_has_object */
	{ "weapon", 2, 1, 0, AI_OPERAND_WEAPON }, /* 0x005e if_weapon_thrown */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x005e if_weapon_thrown */
	{ "weapon", 2, 1, 0, AI_OPERAND_WEAPON }, /* 0x005f if_weapon_thrown_on_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x005f if_weapon_thrown_on_object */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x005f if_weapon_thrown_on_object */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0060 if_chr_has_weapon_equipped */
	{ "weapon", 3, 1, 0, AI_OPERAND_WEAPON }, /* 0x0060 if_chr_has_weapon_equipped */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0060 if_chr_has_weapon_equipped */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0061 if_gun_unclaimed */
	{ "mode", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0061 if_gun_unclaimed */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0061 if_gun_unclaimed */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0062 if_object_healthy */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0062 if_object_healthy */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0063 if_chr_activated_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x0063 if_chr_activated_object */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0063 if_chr_activated_object */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0065 obj_interact */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0066 destroy_object */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0067 ai_0067 */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0068 chr_drop_items */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0069 chr_drop_weapon */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006a give_object_to_chr */
	{ "chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x006a give_object_to_chr */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006b object_move_to_pad */
	{ "pad", 3, 2, 0, AI_OPERAND_PAD }, /* 0x006b object_move_to_pad */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006c open_door */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006d close_door */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006e if_door_state */
	{ "flags", 3, 1, 0, AI_OPERAND_FLAGS }, /* 0x006e if_door_state */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x006e if_door_state */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x006f if_object_is_door */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x006f if_object_is_door */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0070 lock_door */
	{ "flags", 3, 1, 0, AI_OPERAND_FLAGS }, /* 0x0070 lock_door */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0071 unlock_door */
	{ "flags", 3, 1, 0, AI_OPERAND_FLAGS }, /* 0x0071 unlock_door */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0072 if_door_locked */
	{ "flags", 3, 1, 0, AI_OPERAND_FLAGS }, /* 0x0072 if_door_locked */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0072 if_door_locked */
	{ "objective", 2, 1, 0, AI_OPERAND_OBJECTIVE }, /* 0x0073 if_objective_complete */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0073 if_objective_complete */
	{ "objective", 2, 1, 0, AI_OPERAND_OBJECTIVE }, /* 0x0074 if_objective_failed */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0074 if_objective_failed */
	{ "quadrant", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0075 ai_0075 */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0075 ai_0075 */
	{ "quadrant", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0076 set_pad_preset_to_target_quadrant */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0076 set_pad_preset_to_target_quadrant */
	{ "difficulty", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0077 if_difficulty_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0077 if_difficulty_less_than */
	{ "difficulty", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0078 if_difficulty_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0078 if_difficulty_greater_than */
	{ "seconds", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0079 if_stage_timer_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0079 if_stage_timer_less_than */
	{ "seconds", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x007a if_stage_timer_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x007a if_stage_timer_greater_than */
	{ "stage", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x007b if_stage_id_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x007b if_stage_id_less_than */
	{ "stage", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x007c if_stage_id_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x007c if_stage_id_greater_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x007d if_num_arghs_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x007d if_num_arghs_less_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x007e if_num_arghs_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x007e if_num_arghs_greater_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x007f if_num_close_arghs_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x007f if_num_close_arghs_less_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0080 if_num_close_arghs_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0080 if_num_close_arghs_greater_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0081 if_chr_health_greater_than */
	{ "health", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0081 if_chr_health_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0081 if_chr_health_greater_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0082 if_chr_health_less_than */
	{ "health", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0082 if_chr_health_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0082 if_chr_health_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0083 if_injured */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0083 if_injured */
	{ "morale", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0084 set_morale */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0085 add_morale */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0086 chr_add_morale */
	{ "chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x0086 chr_add_morale */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0087 subtract_morale */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0088 if_morale_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0088 if_morale_less_than */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0089 if_morale_less_than_random */
	{ "alertness", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008a set_alertness */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008b add_alertness */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008c chr_add_alertness */
	{ "chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x008c chr_add_alertness */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008d subtract_alertness */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008e if_alertness */
	{ "comparison", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x008e if_alertness */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x008e if_alertness */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x008f if_chr_alertness_less_than */
	{ "chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x008f if_chr_alertness_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x008f if_chr_alertness_less_than */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0090 if_alertness_less_than_random */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0092 set_hear_distance */
	{ "distance", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0093 set_view_distance */
	{ "probability", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0094 set_grenade_probability */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0095 set_chr_num */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0096 set_max_damage */
	{ "max_damage", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x0096 set_max_damage */
	{ "amount", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0097 add_health */
	{ "speed", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x0098 set_reaction_speed */
	{ "speed", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x0099 set_recovery_speed */
	{ "accuracy", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x009a set_accuracy */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x009b set_flag */
	{ "bank", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x009b set_flag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x009c unset_flag */
	{ "bank", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x009c unset_flag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x009d if_has_flag */
	{ "expected", 6, 1, 0, AI_OPERAND_BOOL }, /* 0x009d if_has_flag */
	{ "bank", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x009d if_has_flag */
	{ "label", 8, 1, 0, AI_OPERAND_LABEL }, /* 0x009d if_has_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x009e chr_set_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x009e chr_set_flag */
	{ "bank", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x009e chr_set_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x009f chr_unset_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x009f chr_unset_flag */
	{ "bank", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x009f chr_unset_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00a0 if_chr_has_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a0 if_chr_has_flag */
	{ "bank", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x00a0 if_chr_has_flag */
	{ "label", 8, 1, 0, AI_OPERAND_LABEL }, /* 0x00a0 if_chr_has_flag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a1 set_stage_flag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a2 unset_stage_flag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a3 if_stage_flag_eq */
	{ "expected", 6, 1, 0, AI_OPERAND_BOOL }, /* 0x00a3 if_stage_flag_eq */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x00a3 if_stage_flag_eq */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a4 set_chrflag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a5 unset_chrflag */
	{ "flags", 2, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a6 if_has_chrflag */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x00a6 if_has_chrflag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00a7 chr_set_chrflag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a7 chr_set_chrflag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00a8 chr_unset_chrflag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a8 chr_unset_chrflag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00a9 if_chr_has_chrflag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00a9 if_chr_has_chrflag */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x00a9 if_chr_has_chrflag */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00aa set_obj_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00aa set_obj_flag */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00ab unset_obj_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00ab unset_obj_flag */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00ac if_obj_has_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00ac if_obj_has_flag */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x00ac if_obj_has_flag */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00ad set_obj_flag_2 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00ad set_obj_flag_2 */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00ae unset_obj_flag_2 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00ae unset_obj_flag_2 */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00af if_obj_has_flag_2 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x00af if_obj_has_flag_2 */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x00af if_obj_has_flag_2 */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00b0 set_chr_preset */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00b1 set_chr_target */
	{ "target_chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x00b1 set_chr_target */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x00b2 set_pad_preset */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00b3 chr_set_pad_preset */
	{ "pad", 3, 2, 0, AI_OPERAND_PAD }, /* 0x00b3 chr_set_pad_preset */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00b4 chr_copy_pad_preset */
	{ "target_chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x00b4 chr_copy_pad_preset */
	{ "text", 2, 0, 0, AI_OPERAND_STRING }, /* 0x00b5 print */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00ba if_timer_stopped */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00bb if_timer_greater_than_random */
	{ "time", 2, 3, 0, AI_OPERAND_NUMBER }, /* 0x00bc if_timer_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x00bc if_timer_less_than */
	{ "time", 2, 3, 0, AI_OPERAND_NUMBER }, /* 0x00bd if_timer_greater_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x00bd if_timer_greater_than */
	{ "seconds", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00c0 set_countdown_timer_value */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00c3 if_countdown_timer_stopped */
	{ "seconds", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00c4 if_countdown_timer_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x00c4 if_countdown_timer_less_than */
	{ "seconds", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00c5 if_countdown_timer_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x00c5 if_countdown_timer_greater_than */
	{ "body", 2, 1, 0, AI_OPERAND_BODY }, /* 0x00c6 spawn_chr_at_pad */
	{ "head", 3, 1, 1, AI_OPERAND_HEAD }, /* 0x00c6 spawn_chr_at_pad */
	{ "pad", 4, 2, 0, AI_OPERAND_PAD }, /* 0x00c6 spawn_chr_at_pad */
	{ "ai_list", 6, 2, 0, AI_OPERAND_AI_LIST }, /* 0x00c6 spawn_chr_at_pad */
	{ "flags", 8, 4, 0, AI_OPERAND_FLAGS }, /* 0x00c6 spawn_chr_at_pad */
	{ "label", 12, 1, 0, AI_OPERAND_LABEL }, /* 0x00c6 spawn_chr_at_pad */
	{ "body", 2, 1, 0, AI_OPERAND_BODY }, /* 0x00c7 spawn_chr_at_chr */
	{ "head", 3, 1, 1, AI_OPERAND_HEAD }, /* 0x00c7 spawn_chr_at_chr */
	{ "chr", 4, 1, 0, AI_OPERAND_CHR }, /* 0x00c7 spawn_chr_at_chr */
	{ "ai_list", 5, 2, 0, AI_OPERAND_AI_LIST }, /* 0x00c7 spawn_chr_at_chr */
	{ "flags", 7, 4, 0, AI_OPERAND_FLAGS }, /* 0x00c7 spawn_chr_at_chr */
	{ "label", 11, 1, 0, AI_OPERAND_LABEL }, /* 0x00c7 spawn_chr_at_chr */
	{ "model", 2, 2, 0, AI_OPERAND_MODEL }, /* 0x00c8 try_equip_weapon */
	{ "weapon", 4, 1, 0, AI_OPERAND_WEAPON }, /* 0x00c8 try_equip_weapon */
	{ "flags", 5, 4, 0, AI_OPERAND_FLAGS }, /* 0x00c8 try_equip_weapon */
	{ "label", 9, 1, 0, AI_OPERAND_LABEL }, /* 0x00c8 try_equip_weapon */
	{ "model", 2, 2, 0, AI_OPERAND_MODEL }, /* 0x00c9 try_equip_hat */
	{ "flags", 4, 4, 0, AI_OPERAND_FLAGS }, /* 0x00c9 try_equip_hat */
	{ "label", 8, 1, 0, AI_OPERAND_LABEL }, /* 0x00c9 try_equip_hat */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00ca duplicate_chr */
	{ "ai_list", 3, 2, 0, AI_OPERAND_AI_LIST }, /* 0x00ca duplicate_chr */
	{ "flags", 5, 4, 0, AI_OPERAND_FLAGS }, /* 0x00ca duplicate_chr */
	{ "label", 9, 1, 0, AI_OPERAND_LABEL }, /* 0x00ca duplicate_chr */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00cb show_hudmsg */
	{ "text", 3, 2, 0, AI_OPERAND_TEXT }, /* 0x00cb show_hudmsg */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00cc show_hudmsg_top_middle */
	{ "text", 3, 2, 0, AI_OPERAND_TEXT }, /* 0x00cc show_hudmsg_top_middle */
	{ "colour", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x00cc show_hudmsg_top_middle */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00cd speak */
	{ "text", 3, 2, 1, AI_OPERAND_TEXT }, /* 0x00cd speak */
	{ "sound", 5, 2, 1, AI_OPERAND_SOUND }, /* 0x00cd speak */
	{ "channel", 7, 1, 1, AI_OPERAND_NUMBER }, /* 0x00cd speak */
	{ "colour", 8, 1, 0, AI_OPERAND_NUMBER }, /* 0x00cd speak */
	{ "sound", 2, 2, 1, AI_OPERAND_SOUND }, /* 0x00ce play_sound */
	{ "channel", 4, 1, 1, AI_OPERAND_NUMBER }, /* 0x00ce play_sound */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00cf set_object_sound_playing */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x00cf set_object_sound_playing */
	{ "fade_time60", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x00cf set_object_sound_playing */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x00d0 play_repeating_sound_from_pad */
	{ "pad", 3, 2, 1, AI_OPERAND_PAD }, /* 0x00d0 play_repeating_sound_from_pad */
	{ "sound", 5, 2, 1, AI_OPERAND_SOUND }, /* 0x00d0 play_repeating_sound_from_pad */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00d1 set_object_sound_volume */
	{ "volume", 3, 2, 1, AI_OPERAND_NUMBER }, /* 0x00d1 set_object_sound_volume */
	{ "fade_time60", 5, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d1 set_object_sound_volume */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00d2 set_object_sound_volume_by_distance */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d2 set_object_sound_volume_by_distance */
	{ "fade_time60", 5, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d2 set_object_sound_volume_by_distance */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00d3 audio_mute_channel */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00d4 if_object_sound_volume_less_than */
	{ "volume", 3, 2, 1, AI_OPERAND_NUMBER }, /* 0x00d4 if_object_sound_volume_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x00d4 if_object_sound_volume_less_than */
	{ "path", 2, 1, 0, AI_OPERAND_PATH }, /* 0x00d5 hovercar_begin_path */
	{ "speed_aim", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d6 set_vehicle_speed */
	{ "speed_time", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d6 set_vehicle_speed */
	{ "speed_aim", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d7 set_rotor_speed */
	{ "speed_time", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x00d7 set_rotor_speed */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x00d8 noop_00d8 */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x00d9 noop_00d9 */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00da set_obj_image */
	{ "slot", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x00da set_obj_image */
	{ "image", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x00da set_obj_image */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x00db noop_00db */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x00de warp_jo_to_pad */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00df warp_jo_to_tag */
	{ "has_direction", 3, 2, 0, AI_OPERAND_BOOL }, /* 0x00df warp_jo_to_tag */
	{ "unused", 5, 2, 0, AI_OPERAND_UNUSED }, /* 0x00df warp_jo_to_tag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e0 revoke_control */
	{ "flags", 3, 1, 0, AI_OPERAND_FLAGS }, /* 0x00e0 revoke_control */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e1 grant_control */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e2 chr_move_to_pad */
	{ "pad", 3, 2, 0, AI_OPERAND_PAD }, /* 0x00e2 chr_move_to_pad */
	{ "mode", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x00e2 chr_move_to_pad */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x00e2 chr_move_to_pad */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e3 player_fade_in */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x00e4 players_fade_out */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e5 if_colour_fade_complete */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x00e5 if_colour_fade_complete */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00e8 set_door_open */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00e9 chr_delete_weapon */
	{ "hand", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x00e9 chr_delete_weapon */
	{ "threshold", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00ea if_num_players_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x00ea if_num_players_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00eb if_chr_ammo_quantity_less_than */
	{ "ammo_type", 3, 1, 1, AI_OPERAND_NUMBER }, /* 0x00eb if_chr_ammo_quantity_less_than */
	{ "quantity", 4, 1, 1, AI_OPERAND_NUMBER }, /* 0x00eb if_chr_ammo_quantity_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x00eb if_chr_ammo_quantity_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00ec chr_draw_weapon */
	{ "weapon", 3, 1, 1, AI_OPERAND_WEAPON }, /* 0x00ec chr_draw_weapon */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00ed chr_draw_weapon_in_cutscene */
	{ "weapon", 3, 1, 1, AI_OPERAND_WEAPON }, /* 0x00ed chr_draw_weapon_in_cutscene */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00ee set_player_force_speed */
	{ "speed_x", 3, 1, 1, AI_OPERAND_NUMBER }, /* 0x00ee set_player_force_speed */
	{ "speed_z", 4, 1, 1, AI_OPERAND_NUMBER }, /* 0x00ee set_player_force_speed */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x00ef if_obj_in_room */
	{ "room", 3, 2, 0, AI_OPERAND_ROOM }, /* 0x00ef if_obj_in_room */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x00ef if_obj_in_room */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00f0 ai_00f0 */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00f1 if_attacking */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00f3 chr_set_invincible */
	{ "range", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x00f4 prepare_warp_orbit */
	{ "height", 4, 2, 1, AI_OPERAND_NUMBER }, /* 0x00f4 prepare_warp_orbit */
	{ "rot_angle", 6, 2, 1, AI_OPERAND_NUMBER }, /* 0x00f4 prepare_warp_orbit */
	{ "pad", 8, 2, 0, AI_OPERAND_PAD }, /* 0x00f4 prepare_warp_orbit */
	{ "more_height", 10, 2, 1, AI_OPERAND_NUMBER }, /* 0x00f4 prepare_warp_orbit */
	{ "pos_angle", 12, 2, 0, AI_OPERAND_NUMBER }, /* 0x00f4 prepare_warp_orbit */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00f6 if_warp_latch_complete */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x00f7 if_all_objectives_complete */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00f8 if_player_is_invincible */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x00f8 if_player_is_invincible */
	{ "reason", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00f9 play_x_track */
	{ "min_seconds", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x00f9 play_x_track */
	{ "max_seconds", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x00f9 play_x_track */
	{ "reason", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x00fa stop_x_track */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00fb chr_explosions */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x00fc if_kill_count_greater_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x00fc if_kill_count_greater_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x00fd if_trigger_shot_list */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x00fd if_trigger_shot_list */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x0100 noop_0100 */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x0101 noop_0101 */
	{ "room", 2, 2, 0, AI_OPERAND_ROOM }, /* 0x0102 set_lights */
	{ "operation", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x0102 set_lights */
	{ "brightness_to", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x0102 set_lights */
	{ "brightness_from", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x0102 set_lights */
	{ "duration", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x0102 set_lights */
	{ "unused", 8, 1, 0, AI_OPERAND_UNUSED }, /* 0x0102 set_lights */
	{ "unused2", 9, 1, 0, AI_OPERAND_UNUSED }, /* 0x0102 set_lights */
	{ "unused3", 10, 1, 0, AI_OPERAND_UNUSED }, /* 0x0102 set_lights */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0103 if_prop_preset_blocking_sight_to_target */
	{ "height", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0105 if_prop_preset_height_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0105 if_prop_preset_height_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0106 set_target */
	{ "mode", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0106 set_target */
	{ "flags", 4, 1, 0, AI_OPERAND_FLAGS }, /* 0x0106 set_target */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0107 if_presets_target_is_not_my_target */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0108 if_chr_target */
	{ "target_chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x0108 if_chr_target */
	{ "any_target", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x0108 if_chr_target */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0108 if_chr_target */
	{ "check_type", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0109 set_chr_preset_to_chr_near_self */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x0109 set_chr_preset_to_chr_near_self */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0109 set_chr_preset_to_chr_near_self */
	{ "check_type", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x010a set_chr_preset_to_chr_near_pad */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x010a set_chr_preset_to_chr_near_pad */
	{ "pad", 5, 2, 0, AI_OPERAND_PAD }, /* 0x010a set_chr_preset_to_chr_near_pad */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x010a set_chr_preset_to_chr_near_pad */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x010b chr_set_team */
	{ "team", 3, 1, 0, AI_OPERAND_TEAM }, /* 0x010b chr_set_team */
	{ "check_type", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x010c if_compare_chr_presets_team */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x010c if_compare_chr_presets_team */
	{ "shield", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x010e set_shield */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x010f if_chr_shield_less_than */
	{ "shield", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x010f if_chr_shield_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x010f if_chr_shield_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0110 if_chr_shield_greater_than */
	{ "shield", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x0110 if_chr_shield_greater_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0110 if_chr_shield_greater_than */
	{ "anim", 2, 2, 1, AI_OPERAND_ANIM }, /* 0x0111 set_camera_animation */
	{ "anim", 2, 2, 1, AI_OPERAND_ANIM }, /* 0x0112 object_do_animation */
	{ "object", 4, 1, 0, AI_OPERAND_OBJECT }, /* 0x0112 object_do_animation */
	{ "speed_divisor", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x0112 object_do_animation */
	{ "start_frame", 6, 2, 0, AI_OPERAND_NUMBER }, /* 0x0112 object_do_animation */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0113 if_in_cutscene */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0114 enable_chr */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0115 disable_chr */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0116 enable_obj */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0117 disable_obj */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0118 set_obj_flag_3 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x0118 set_obj_flag_3 */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0119 unset_obj_flag_3 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x0119 unset_obj_flag_3 */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x011a if_obj_has_flag_3 */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x011a if_obj_has_flag_3 */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x011a if_obj_has_flag_3 */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x011b chr_set_hidden_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x011b chr_set_hidden_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x011c chr_unset_hidden_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x011c chr_unset_hidden_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x011d if_chr_has_hidden_flag */
	{ "flags", 3, 4, 0, AI_OPERAND_FLAGS }, /* 0x011d if_chr_has_hidden_flag */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x011d if_chr_has_hidden_flag */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x011e if_human */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x011e if_human */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x011f if_skedar */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x011f if_skedar */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0120 if_safety_2_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0120 if_safety_2_less_than */
	{ "criteria", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0121 find_cover */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0121 find_cover */
	{ "criteria", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0122 find_cover_within_dist */
	{ "distance", 4, 4, 0, AI_OPERAND_NUMBER }, /* 0x0122 find_cover_within_dist */
	{ "label", 8, 1, 0, AI_OPERAND_LABEL }, /* 0x0122 find_cover_within_dist */
	{ "criteria", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x0123 find_cover_outside_dist */
	{ "distance", 4, 4, 0, AI_OPERAND_NUMBER }, /* 0x0123 find_cover_outside_dist */
	{ "label", 8, 1, 0, AI_OPERAND_LABEL }, /* 0x0123 find_cover_outside_dist */
	{ "speed", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0124 go_to_cover */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0125 check_cover_out_of_sight */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0126 if_player_using_cmp_or_ar_34 */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0127 detect_enemy_on_same_floor */
	{ "distance", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0128 detect_enemy */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0128 detect_enemy */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0129 if_safety_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0129 if_safety_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x012a if_target_moving_slowly */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x012a if_target_moving_slowly */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x012b if_target_moving_closer */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x012c if_target_moving_away */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0130 say_quip */
	{ "quip", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "probability", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "sound_gap", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "nearby_mode", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "flags", 7, 1, 0, AI_OPERAND_FLAGS }, /* 0x0130 say_quip */
	{ "text_row", 8, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "colour", 9, 1, 0, AI_OPERAND_NUMBER }, /* 0x0130 say_quip */
	{ "amount", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0131 increase_squadron_alertness */
	{ "action", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0132 set_action */
	{ "keep_orders", 3, 1, 0, AI_OPERAND_BOOL }, /* 0x0132 set_action */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x0133 set_team_orders */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0133 set_team_orders */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x0134 if_orders */
	{ "orders", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0134 if_orders */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0134 if_orders */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0135 if_has_orders */
	{ "speed", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0136 retreat */
	{ "operation", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0136 retreat */
	{ "action", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0137 if_chr_in_squadron_doing_action */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0137 if_chr_in_squadron_doing_action */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x0138 if_channel_free */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0138 if_channel_free */
	{ "angle", 2, 4, 0, AI_OPERAND_NUMBER }, /* 0x0139 ai_0139 */
	{ "speed", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x0139 ai_0139 */
	{ "try_alternate", 7, 1, 0, AI_OPERAND_BOOL }, /* 0x0139 ai_0139 */
	{ "distance", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x013a set_chr_preset_to_unalerted_teammate */
	{ "mode", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x013a set_chr_preset_to_unalerted_teammate */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x013a set_chr_preset_to_unalerted_teammate */
	{ "squadron", 2, 1, 0, AI_OPERAND_SQUADRON }, /* 0x013b set_squadron */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x013c face_cover */
	{ "flags", 2, 1, 0, AI_OPERAND_FLAGS }, /* 0x013d if_dangerous_object_nearby */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x013d if_dangerous_object_nearby */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x013f if_heli_weapons_armed */
	{ "comparison", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0140 if_hoverbot_next_step */
	{ "threshold", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0140 if_hoverbot_next_step */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0140 if_hoverbot_next_step */
	{ "good_terminal", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "alarm_terminal", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "terminal1", 4, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "terminal2", 5, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "terminal3", 6, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "terminal4", 7, 1, 0, AI_OPERAND_OBJECT }, /* 0x0141 shuffle_investigation_terminals */
	{ "disabled", 8, 1, 0, AI_OPERAND_BOOL }, /* 0x0141 shuffle_investigation_terminals */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0142 set_pad_preset_to_investigation_terminal */
	{ "unused", 3, 1, 0, AI_OPERAND_UNUSED }, /* 0x0142 set_pad_preset_to_investigation_terminal */
	{ "squadron", 2, 1, 0, AI_OPERAND_SQUADRON }, /* 0x0147 if_squadron_is_dead */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0147 if_squadron_is_dead */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0148 chr_set_listening */
	{ "listening", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0148 chr_set_listening */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0149 if_chr_listening */
	{ "listening", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0149 if_chr_listening */
	{ "check_convtalk", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x0149 if_chr_listening */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0149 if_chr_listening */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x014a if_true */
	{ "unused2", 3, 1, 0, AI_OPERAND_UNUSED }, /* 0x014a if_true */
	{ "unused3", 4, 1, 0, AI_OPERAND_UNUSED }, /* 0x014a if_true */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x014a if_true */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x014b if_not_listening */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0152 if_num_chrs_in_squadron_greater_than */
	{ "squadron", 3, 1, 0, AI_OPERAND_SQUADRON }, /* 0x0152 if_num_chrs_in_squadron_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0152 if_num_chrs_in_squadron_greater_than */
	{ "enabled", 2, 1, 0, AI_OPERAND_BOOL }, /* 0x0157 set_tinted_glass_enabled */
	{ "track", 2, 1, 0, AI_OPERAND_MUSIC }, /* 0x015b play_track_isolated */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0165 if_chr_injured_target */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0165 if_chr_injured_target */
	{ "action", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0166 if_action */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0166 if_action */
	{ "side", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0167 hovercopter_fire_rocket */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0168 if_shield_damaged */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0168 if_shield_damaged */
	{ "natural_anim", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0169 if_natural_anim */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0169 if_natural_anim */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x016a if_y */
	{ "y", 3, 2, 1, AI_OPERAND_NUMBER }, /* 0x016a if_y */
	{ "comparison", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x016a if_y */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x016a if_y */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x016b play_repeating_sound_from_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x016b play_repeating_sound_from_object */
	{ "fade_time60", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x016b play_repeating_sound_from_object */
	{ "falloff_distance", 6, 2, 0, AI_OPERAND_NUMBER }, /* 0x016b play_repeating_sound_from_object */
	{ "max_distance", 8, 2, 0, AI_OPERAND_NUMBER }, /* 0x016b play_repeating_sound_from_object */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x016d chr_adjust_motion_blur */
	{ "amount", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x016d chr_adjust_motion_blur */
	{ "increase", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x016d chr_adjust_motion_blur */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x016e damage_chr_by_amount */
	{ "amount", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x016e damage_chr_by_amount */
	{ "operation", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x016e damage_chr_by_amount */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x016f if_chr_has_no_gun */
	{ "unused", 3, 1, 0, AI_OPERAND_UNUSED }, /* 0x016f if_chr_has_no_gun */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x016f if_chr_has_no_gun */
	{ "operation", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0170 do_gun_command */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0170 do_gun_command */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0171 if_distance_to_gun_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0171 if_distance_to_gun_less_than */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x0172 recover_gun */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0172 recover_gun */
	{ "source_chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0173 chr_copy_properties */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0173 chr_copy_properties */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0174 if_cutscene_button_pressed */
	{ "duration", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0175 reorient_for_cutscene_stop */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0176 if_bot_respawning */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0177 player_auto_walk */
	{ "pad", 3, 2, 1, AI_OPERAND_PAD }, /* 0x0177 player_auto_walk */
	{ "walk_speed", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x0177 player_auto_walk */
	{ "turn_speed", 6, 1, 0, AI_OPERAND_NUMBER }, /* 0x0177 player_auto_walk */
	{ "look_up", 7, 1, 0, AI_OPERAND_NUMBER }, /* 0x0177 player_auto_walk */
	{ "distance", 8, 1, 0, AI_OPERAND_NUMBER }, /* 0x0177 player_auto_walk */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0178 if_player_auto_walk_finished */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0178 if_player_auto_walk_finished */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x0179 play_sound_from_entity */
	{ "source", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0179 play_sound_from_entity */
	{ "fade_time60", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x0179 play_sound_from_entity */
	{ "falloff_distance", 6, 2, 0, AI_OPERAND_NUMBER }, /* 0x0179 play_sound_from_entity */
	{ "max_distance", 8, 2, 0, AI_OPERAND_NUMBER }, /* 0x0179 play_sound_from_entity */
	{ "source_is_chr", 10, 1, 0, AI_OPERAND_BOOL }, /* 0x0179 play_sound_from_entity */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x017a if_los_to_attack_target */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x017b if_chr_knocked_out */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x017b if_chr_knocked_out */
	{ "sound", 2, 2, 1, AI_OPERAND_SOUND }, /* 0x017c assign_sound */
	{ "channel", 4, 1, 1, AI_OPERAND_NUMBER }, /* 0x017c assign_sound */
	{ "track", 2, 1, 0, AI_OPERAND_MUSIC }, /* 0x017d play_cutscene_track */
	{ "track", 2, 1, 0, AI_OPERAND_MUSIC }, /* 0x017f play_temporary_track */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x0181 if_player_looking_at_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x0181 if_player_looking_at_object */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x0181 if_player_looking_at_object */
	{ "reverse", 2, 1, 0, AI_OPERAND_BOOL }, /* 0x0182 punch_or_kick */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0182 punch_or_kick */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0183 if_target_is_player */
	{ "min_percent", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0184 try_attack_amount */
	{ "max_percent", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0184 try_attack_amount */
	{ "time", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x0186 if_sound_timer */
	{ "comparison", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x0186 if_sound_timer */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x0186 if_sound_timer */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x0187 set_target_to_eyespy_if_in_sight */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0188 if_lift_stationary */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0188 if_lift_stationary */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x0189 lift_go_to_stop */
	{ "stop", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x0189 lift_go_to_stop */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x018a if_lift_at_stop */
	{ "stop", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x018a if_lift_at_stop */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x018a if_lift_at_stop */
	{ "intensity", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x018b configure_rain */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x018c chr_toggle_model_part */
	{ "part", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x018c chr_toggle_model_part */
	{ "lift", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x018d activate_lift */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x018d activate_lift */
	{ "unknown1", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x018e mini_skedar_try_pounce */
	{ "unknown2", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x018e mini_skedar_try_pounce */
	{ "unknown3", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x018e mini_skedar_try_pounce */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x018e mini_skedar_try_pounce */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x018f if_object_distance_to_pad_less_than */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x018f if_object_distance_to_pad_less_than */
	{ "pad", 5, 2, 0, AI_OPERAND_PAD }, /* 0x018f if_object_distance_to_pad_less_than */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x018f if_object_distance_to_pad_less_than */
	{ "flag", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0190 set_savefile_flag */
	{ "flag", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0191 unset_savefile_flag */
	{ "flag", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0192 if_savefile_flag_is_set */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0192 if_savefile_flag_is_set */
	{ "flag", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x0193 if_savefile_flag_is_unset */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x0193 if_savefile_flag_is_unset */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x019e if_obj_health_less_than */
	{ "damage", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x019e if_obj_health_less_than */
	{ "label", 5, 1, 0, AI_OPERAND_LABEL }, /* 0x019e if_obj_health_less_than */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x019f set_obj_health */
	{ "damage", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x019f set_obj_health */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01a0 set_chr_special_death_animation */
	{ "special_death", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a0 set_chr_special_death_animation */
	{ "quip", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a2 say_ci_staff_quip */
	{ "channel", 3, 1, 1, AI_OPERAND_NUMBER }, /* 0x01a2 say_ci_staff_quip */
	{ "preset", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a3 do_preset_animation */
	{ "operation", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a4 show_hudmsg_middle */
	{ "colour", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a4 show_hudmsg_middle */
	{ "text", 4, 2, 0, AI_OPERAND_TEXT }, /* 0x01a4 show_hudmsg_middle */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01a5 if_using_lift */
	{ "distance", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01a6 if_target_y_difference_less_than */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x01a6 if_target_y_difference_less_than */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01a7 if_chr_not_talking */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x01a7 if_chr_not_talking */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01aa if_player_chr_portal_distance_less_than */
	{ "threshold", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01ab if_num_knocked_out_chrs */
	{ "comparison", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x01ab if_num_knocked_out_chrs */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x01ab if_num_knocked_out_chrs */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x01ad release_object */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x01ae clear_inventory */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01af chr_grab_object */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x01af chr_grab_object */
	{ "pillar_target1", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar_target2", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar_target3", 4, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar1", 5, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar2", 6, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar3", 7, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar4", 8, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "pillar5", 9, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine_target1", 10, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine_target2", 11, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine_target3", 12, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine1", 13, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine2", 14, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine3", 15, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine4", 16, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "mine5", 17, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b1 shuffle_ruins_pillars */
	{ "speed", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01b2 set_wind_speed */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01b3 toggle_p1p2 */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01b4 if_chr_reposition_valid */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01b5 chr_set_p1p2 */
	{ "player_chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x01b5 chr_set_p1p2 */
	{ "intensity", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01b6 configure_snow */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01b7 chr_set_cloaked */
	{ "cloaked", 3, 1, 0, AI_OPERAND_BOOL }, /* 0x01b7 chr_set_cloaked */
	{ "play_sound", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x01b7 chr_set_cloaked */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x01b8 set_autogun_target_team */
	{ "team", 3, 1, 0, AI_OPERAND_TEAM }, /* 0x01b8 set_autogun_target_team */
	{ "flags", 2, 2, 0, AI_OPERAND_FLAGS }, /* 0x01ba try_attack_lie */
	{ "target", 4, 2, 0, AI_OPERAND_NUMBER }, /* 0x01ba try_attack_lie */
	{ "label", 6, 1, 0, AI_OPERAND_LABEL }, /* 0x01ba try_attack_lie */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x01bb noop_01bb */
	{ "unused2", 3, 1, 0, AI_OPERAND_UNUSED }, /* 0x01bb noop_01bb */
	{ "pounce_bits", 2, 1, 0, AI_OPERAND_FLAGS }, /* 0x01bc if_pouncebits_eq */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x01bc if_pouncebits_eq */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01bd if_training_pc_holographed */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01be if_player_using_device */
	{ "weapon", 3, 1, 0, AI_OPERAND_WEAPON }, /* 0x01be if_player_using_device */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x01be if_player_using_device */
	{ "pad", 2, 2, 0, AI_OPERAND_PAD }, /* 0x01bf chr_begin_or_end_teleport */
	{ "chr", 4, 1, 0, AI_OPERAND_CHR }, /* 0x01bf chr_begin_or_end_teleport */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01c0 if_chr_teleport_full_white */
	{ "chr", 3, 1, 0, AI_OPERAND_CHR }, /* 0x01c0 if_chr_teleport_full_white */
	{ "ai_list", 2, 2, 0, AI_OPERAND_AI_LIST }, /* 0x01c1 set_punch_dodge_list */
	{ "ai_list", 2, 2, 0, AI_OPERAND_AI_LIST }, /* 0x01c2 set_shooting_at_me_list */
	{ "ai_list", 2, 2, 0, AI_OPERAND_AI_LIST }, /* 0x01c3 set_dark_room_list */
	{ "ai_list", 2, 2, 0, AI_OPERAND_AI_LIST }, /* 0x01c4 set_player_dead_list */
	{ "mode", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x01c6 set_dodge_rating */
	{ "rating", 3, 1, 1, AI_OPERAND_NUMBER }, /* 0x01c6 set_dodge_rating */
	{ "rating", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x01c7 set_unarmed_dodge_rating */
	{ "mode", 2, 1, 0, AI_OPERAND_NUMBER }, /* 0x01c8 title_init_mode */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01c9 try_exit_title */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01ca chr_set_cutscene_weapon */
	{ "weapon", 3, 1, 0, AI_OPERAND_WEAPON }, /* 0x01ca chr_set_cutscene_weapon */
	{ "fallback_weapon", 4, 1, 0, AI_OPERAND_WEAPON }, /* 0x01ca chr_set_cutscene_weapon */
	{ "colour", 2, 4, 0, AI_OPERAND_NUMBER }, /* 0x01cb fade_screen */
	{ "duration", 6, 2, 1, AI_OPERAND_NUMBER }, /* 0x01cb fade_screen */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01cc if_fade_complete */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01cd set_chr_hudpiece_visible */
	{ "visible", 3, 1, 0, AI_OPERAND_BOOL }, /* 0x01cd set_chr_hudpiece_visible */
	{ "enabled", 2, 1, 0, AI_OPERAND_BOOL }, /* 0x01ce set_passive_mode */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01cf chr_set_firing_in_cutscene */
	{ "firing", 3, 1, 0, AI_OPERAND_BOOL }, /* 0x01cf chr_set_firing_in_cutscene */
	{ "portal", 2, 2, 1, AI_OPERAND_PORTAL }, /* 0x01d0 set_portal_flag */
	{ "flags", 4, 1, 0, AI_OPERAND_FLAGS }, /* 0x01d0 set_portal_flag */
	{ "object", 2, 1, 0, AI_OPERAND_OBJECT }, /* 0x01d1 obj_set_model_part_visible */
	{ "part", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d1 obj_set_model_part_visible */
	{ "visible", 4, 1, 0, AI_OPERAND_BOOL }, /* 0x01d1 obj_set_model_part_visible */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01d2 chr_emit_sparks */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01d3 set_dr_caroll_images */
	{ "right_image", 3, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d3 set_dr_caroll_images */
	{ "left_image", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d3 set_dr_caroll_images */
	{ "room", 2, 2, 1, AI_OPERAND_ROOM }, /* 0x01d4 set_room_flag */
	{ "flags", 4, 2, 1, AI_OPERAND_FLAGS }, /* 0x01d4 set_room_flag */
	{ "show", 2, 1, 0, AI_OPERAND_BOOL }, /* 0x01d5 show_cutscene_chrs */
	{ "room", 2, 2, 1, AI_OPERAND_ROOM }, /* 0x01d6 configure_environment */
	{ "operation", 4, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d6 configure_environment */
	{ "value", 5, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d6 configure_environment */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x01d7 if_distance_to_target_2_less_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x01d7 if_distance_to_target_2_less_than */
	{ "distance", 2, 2, 0, AI_OPERAND_NUMBER }, /* 0x01d8 if_distance_to_target_2_greater_than */
	{ "label", 4, 1, 0, AI_OPERAND_LABEL }, /* 0x01d8 if_distance_to_target_2_greater_than */
	{ "channel", 2, 1, 1, AI_OPERAND_NUMBER }, /* 0x01d9 play_sound_from_prop */
	{ "object", 3, 1, 0, AI_OPERAND_OBJECT }, /* 0x01d9 play_sound_from_prop */
	{ "sound", 4, 2, 1, AI_OPERAND_SOUND }, /* 0x01d9 play_sound_from_prop */
	{ "volume", 6, 2, 0, AI_OPERAND_NUMBER }, /* 0x01d9 play_sound_from_prop */
	{ "type", 8, 1, 0, AI_OPERAND_NUMBER }, /* 0x01d9 play_sound_from_prop */
	{ "unused", 9, 1, 0, AI_OPERAND_UNUSED }, /* 0x01d9 play_sound_from_prop */
	{ "flags", 10, 1, 0, AI_OPERAND_FLAGS }, /* 0x01d9 play_sound_from_prop */
	{ "track", 2, 1, 0, AI_OPERAND_MUSIC }, /* 0x01da play_temporary_primary_track */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01db chr_kill */
	{ "weapon", 2, 1, 0, AI_OPERAND_WEAPON }, /* 0x01dc remove_weapon_from_inventory */
	{ "unused", 2, 1, 0, AI_OPERAND_UNUSED }, /* 0x01dd if_music_event_queue_is_empty */
	{ "label", 3, 1, 0, AI_OPERAND_LABEL }, /* 0x01dd if_music_event_queue_is_empty */
	{ "label", 2, 1, 0, AI_OPERAND_LABEL }, /* 0x01de if_coop_mode */
	{ "chr", 2, 1, 0, AI_OPERAND_CHR }, /* 0x01df if_chr_same_floor_distance_to_pad_less_than */
	{ "distance", 3, 2, 0, AI_OPERAND_NUMBER }, /* 0x01df if_chr_same_floor_distance_to_pad_less_than */
	{ "pad", 5, 2, 0, AI_OPERAND_PAD }, /* 0x01df if_chr_same_floor_distance_to_pad_less_than */
	{ "label", 7, 1, 0, AI_OPERAND_LABEL }, /* 0x01df if_chr_same_floor_distance_to_pad_less_than */
};

/* In opcode order (aiCommandSchemaFind searches it). */
static const ai_command_schema_t s_AiCommands[] = {
	{ 0x0000, "go_to_next", 3, 1, &s_AiCommandFields[0] },
	{ 0x0001, "go_to_first", 3, 1, &s_AiCommandFields[1] },
	{ 0x0002, "label", 3, 1, &s_AiCommandFields[2] },
	{ 0x0003, "yield", 2, 0, NULL },
	{ 0x0004, "end", 2, 0, NULL },
	{ 0x0005, "set_list", 5, 2, &s_AiCommandFields[3] },
	{ 0x0006, "set_return_list", 5, 2, &s_AiCommandFields[5] },
	{ 0x0007, "set_shot_list", 4, 1, &s_AiCommandFields[7] },
	{ 0x0008, "return", 2, 0, NULL },
	{ 0x0009, "stop", 2, 0, NULL },
	{ 0x000a, "kneel", 2, 0, NULL },
	{ 0x000b, "chr_do_animation", 12, 7, &s_AiCommandFields[8] },
	{ 0x000c, "if_idle", 3, 1, &s_AiCommandFields[15] },
	{ 0x000d, "be_surprised_one_hand", 2, 0, NULL },
	{ 0x000e, "be_surprised_look_around", 2, 0, NULL },
	{ 0x000f, "try_sidestep", 3, 1, &s_AiCommandFields[16] },
	{ 0x0010, "try_jump_out", 3, 1, &s_AiCommandFields[17] },
	{ 0x0011, "try_run_sideways", 3, 1, &s_AiCommandFields[18] },
	{ 0x0012, "try_attack_walk", 3, 1, &s_AiCommandFields[19] },
	{ 0x0013, "try_attack_run", 3, 1, &s_AiCommandFields[20] },
	{ 0x0014, "try_attack_roll", 3, 1, &s_AiCommandFields[21] },
	{ 0x0015, "try_attack_stand", 7, 3, &s_AiCommandFields[22] },
	{ 0x0016, "try_attack_kneel", 7, 3, &s_AiCommandFields[25] },
	{ 0x0017, "try_modify_attack", 7, 3, &s_AiCommandFields[28] },
	{ 0x0018, "face_entity", 7, 3, &s_AiCommandFields[31] },
	{ 0x0019, "ai_0019", 8, 6, &s_AiCommandFields[34] },
	{ 0x001a, "chr_damage_chr", 5, 3, &s_AiCommandFields[40] },
	{ 0x001b, "consider_grenade_throw", 7, 3, &s_AiCommandFields[43] },
	{ 0x001c, "drop_item", 6, 3, &s_AiCommandFields[46] },
	{ 0x001d, "jog_to_pad", 4, 1, &s_AiCommandFields[49] },
	{ 0x001e, "go_to_pad_preset", 3, 1, &s_AiCommandFields[50] },
	{ 0x001f, "walk_to_pad", 4, 1, &s_AiCommandFields[51] },
	{ 0x0020, "run_to_pad", 4, 1, &s_AiCommandFields[52] },
	{ 0x0021, "set_path", 3, 1, &s_AiCommandFields[53] },
	{ 0x0022, "start_patrol", 2, 0, NULL },
	{ 0x0023, "if_patrolling", 3, 1, &s_AiCommandFields[54] },
	{ 0x0024, "surrender", 2, 0, NULL },
	{ 0x0025, "fade_out", 2, 0, NULL },
	{ 0x0026, "remove_chr", 3, 1, &s_AiCommandFields[55] },
	{ 0x0027, "try_start_alarm", 5, 2, &s_AiCommandFields[56] },
	{ 0x0028, "activate_alarm", 2, 0, NULL },
	{ 0x0029, "deactivate_alarm", 2, 0, NULL },
	{ 0x002a, "try_run_from_target", 3, 1, &s_AiCommandFields[58] },
	{ 0x002b, "try_jog_to_target_prop", 3, 1, &s_AiCommandFields[59] },
	{ 0x002c, "try_walk_to_target_prop", 3, 1, &s_AiCommandFields[60] },
	{ 0x002d, "try_run_to_target_prop", 3, 1, &s_AiCommandFields[61] },
	{ 0x002e, "try_go_to_cover_prop", 3, 1, &s_AiCommandFields[62] },
	{ 0x002f, "try_jog_to_chr", 4, 2, &s_AiCommandFields[63] },
	{ 0x0030, "try_walk_to_chr", 4, 2, &s_AiCommandFields[65] },
	{ 0x0031, "try_run_to_chr", 4, 2, &s_AiCommandFields[67] },
	{ 0x0032, "if_stopped", 3, 1, &s_AiCommandFields[69] },
	{ 0x0033, "if_chr_dead", 4, 2, &s_AiCommandFields[70] },
	{ 0x0034, "if_chr_death_animation_finished", 4, 2, &s_AiCommandFields[72] },
	{ 0x0035, "if_can_see_target", 3, 1, &s_AiCommandFields[74] },
	{ 0x0036, "random", 2, 0, NULL },
	{ 0x0037, "if_random_less_than", 4, 2, &s_AiCommandFields[75] },
	{ 0x0038, "if_random_greater_than", 4, 2, &s_AiCommandFields[77] },
	{ 0x0039, "if_can_hear_alarm", 3, 1, &s_AiCommandFields[79] },
	{ 0x003a, "if_alarm_active", 3, 1, &s_AiCommandFields[80] },
	{ 0x003b, "if_gas_active", 3, 1, &s_AiCommandFields[81] },
	{ 0x003c, "if_hears_target", 3, 1, &s_AiCommandFields[82] },
	{ 0x003d, "if_saw_injury", 4, 2, &s_AiCommandFields[83] },
	{ 0x003e, "if_saw_death", 4, 2, &s_AiCommandFields[85] },
	{ 0x003f, "if_los_to_target", 3, 1, &s_AiCommandFields[87] },
	{ 0x0040, "if_target_nearly_in_sight", 7, 2, &s_AiCommandFields[88] },
	{ 0x0041, "if_nearly_in_targets_sight", 7, 2, &s_AiCommandFields[90] },
	{ 0x0042, "set_pad_preset_to_pad_on_route_to_target", 3, 1, &s_AiCommandFields[92] },
	{ 0x0043, "if_saw_target_recently", 3, 1, &s_AiCommandFields[93] },
	{ 0x0044, "if_heard_target_recently", 3, 1, &s_AiCommandFields[94] },
	{ 0x0045, "if_los_to_chr", 4, 2, &s_AiCommandFields[95] },
	{ 0x0046, "if_never_been_on_screen", 3, 1, &s_AiCommandFields[97] },
	{ 0x0047, "if_on_screen", 3, 1, &s_AiCommandFields[98] },
	{ 0x0048, "if_chr_in_on_screen_room", 4, 2, &s_AiCommandFields[99] },
	{ 0x0049, "if_room_is_on_screen", 5, 2, &s_AiCommandFields[101] },
	{ 0x004a, "if_target_aiming_at_me", 3, 1, &s_AiCommandFields[103] },
	{ 0x004b, "if_near_miss", 3, 1, &s_AiCommandFields[104] },
	{ 0x004c, "if_sees_suspicious_item", 3, 1, &s_AiCommandFields[105] },
	{ 0x004d, "if_target_in_fov_left", 4, 2, &s_AiCommandFields[106] },
	{ 0x004e, "if_check_fov_with_target", 6, 4, &s_AiCommandFields[108] },
	{ 0x004f, "if_target_out_of_fov_left", 4, 2, &s_AiCommandFields[112] },
	{ 0x0050, "if_target_in_fov", 4, 2, &s_AiCommandFields[114] },
	{ 0x0051, "if_target_out_of_fov", 4, 2, &s_AiCommandFields[116] },
	{ 0x0052, "if_distance_to_target_less_than", 5, 2, &s_AiCommandFields[118] },
	{ 0x0053, "if_distance_to_target_greater_than", 5, 2, &s_AiCommandFields[120] },
	{ 0x0054, "if_chr_distance_to_pad_less_than", 8, 4, &s_AiCommandFields[122] },
	{ 0x0055, "if_chr_distance_to_pad_greater_than", 8, 4, &s_AiCommandFields[126] },
	{ 0x0056, "if_distance_to_chr_less_than", 6, 3, &s_AiCommandFields[130] },
	{ 0x0057, "if_distance_to_chr_greater_than", 6, 3, &s_AiCommandFields[133] },
	{ 0x0058, "ai_0058", 5, 2, &s_AiCommandFields[136] },
	{ 0x0059, "if_distance_from_target_to_pad_less_than", 7, 3, &s_AiCommandFields[138] },
	{ 0x005a, "if_distance_from_target_to_pad_greater_than", 7, 3, &s_AiCommandFields[141] },
	{ 0x005b, "if_chr_in_room", 7, 4, &s_AiCommandFields[144] },
	{ 0x005c, "if_target_in_room", 5, 2, &s_AiCommandFields[148] },
	{ 0x005d, "if_chr_has_object", 5, 3, &s_AiCommandFields[150] },
	{ 0x005e, "if_weapon_thrown", 4, 2, &s_AiCommandFields[153] },
	{ 0x005f, "if_weapon_thrown_on_object", 5, 3, &s_AiCommandFields[155] },
	{ 0x0060, "if_chr_has_weapon_equipped", 5, 3, &s_AiCommandFields[158] },
	{ 0x0061, "if_gun_unclaimed", 5, 3, &s_AiCommandFields[161] },
	{ 0x0062, "if_object_healthy", 4, 2, &s_AiCommandFields[164] },
	{ 0x0063, "if_chr_activated_object", 5, 3, &s_AiCommandFields[166] },
	{ 0x0065, "obj_interact", 3, 1, &s_AiCommandFields[169] },
	{ 0x0066, "destroy_object", 3, 1, &s_AiCommandFields[170] },
	{ 0x0067, "ai_0067", 3, 1, &s_AiCommandFields[171] },
	{ 0x0068, "chr_drop_items", 3, 1, &s_AiCommandFields[172] },
	{ 0x0069, "chr_drop_weapon", 3, 1, &s_AiCommandFields[173] },
	{ 0x006a, "give_object_to_chr", 4, 2, &s_AiCommandFields[174] },
	{ 0x006b, "object_move_to_pad", 5, 2, &s_AiCommandFields[176] },
	{ 0x006c, "open_door", 3, 1, &s_AiCommandFields[178] },
	{ 0x006d, "close_door", 3, 1, &s_AiCommandFields[179] },
	{ 0x006e, "if_door_state", 5, 3, &s_AiCommandFields[180] },
	{ 0x006f, "if_object_is_door", 4, 2, &s_AiCommandFields[183] },
	{ 0x0070, "lock_door", 4, 2, &s_AiCommandFields[185] },
	{ 0x0071, "unlock_door", 4, 2, &s_AiCommandFields[187] },
	{ 0x0072, "if_door_locked", 5, 3, &s_AiCommandFields[189] },
	{ 0x0073, "if_objective_complete", 4, 2, &s_AiCommandFields[192] },
	{ 0x0074, "if_objective_failed", 4, 2, &s_AiCommandFields[194] },
	{ 0x0075, "ai_0075", 4, 2, &s_AiCommandFields[196] },
	{ 0x0076, "set_pad_preset_to_target_quadrant", 4, 2, &s_AiCommandFields[198] },
	{ 0x0077, "if_difficulty_less_than", 4, 2, &s_AiCommandFields[200] },
	{ 0x0078, "if_difficulty_greater_than", 4, 2, &s_AiCommandFields[202] },
	{ 0x0079, "if_stage_timer_less_than", 5, 2, &s_AiCommandFields[204] },
	{ 0x007a, "if_stage_timer_greater_than", 5, 2, &s_AiCommandFields[206] },
	{ 0x007b, "if_stage_id_less_than", 4, 2, &s_AiCommandFields[208] },
	{ 0x007c, "if_stage_id_greater_than", 4, 2, &s_AiCommandFields[210] },
	{ 0x007d, "if_num_arghs_less_than", 4, 2, &s_AiCommandFields[212] },
	{ 0x007e, "if_num_arghs_greater_than", 4, 2, &s_AiCommandFields[214] },
	{ 0x007f, "if_num_close_arghs_less_than", 4, 2, &s_AiCommandFields[216] },
	{ 0x0080, "if_num_close_arghs_greater_than", 4, 2, &s_AiCommandFields[218] },
	{ 0x0081, "if_chr_health_greater_than", 5, 3, &s_AiCommandFields[220] },
	{ 0x0082, "if_chr_health_less_than", 5, 3, &s_AiCommandFields[223] },
	{ 0x0083, "if_injured", 4, 2, &s_AiCommandFields[226] },
	{ 0x0084, "set_morale", 3, 1, &s_AiCommandFields[228] },
	{ 0x0085, "add_morale", 3, 1, &s_AiCommandFields[229] },
	{ 0x0086, "chr_add_morale", 4, 2, &s_AiCommandFields[230] },
	{ 0x0087, "subtract_morale", 3, 1, &s_AiCommandFields[232] },
	{ 0x0088, "if_morale_less_than", 4, 2, &s_AiCommandFields[233] },
	{ 0x0089, "if_morale_less_than_random", 3, 1, &s_AiCommandFields[235] },
	{ 0x008a, "set_alertness", 3, 1, &s_AiCommandFields[236] },
	{ 0x008b, "add_alertness", 3, 1, &s_AiCommandFields[237] },
	{ 0x008c, "chr_add_alertness", 4, 2, &s_AiCommandFields[238] },
	{ 0x008d, "subtract_alertness", 3, 1, &s_AiCommandFields[240] },
	{ 0x008e, "if_alertness", 5, 3, &s_AiCommandFields[241] },
	{ 0x008f, "if_chr_alertness_less_than", 5, 3, &s_AiCommandFields[244] },
	{ 0x0090, "if_alertness_less_than_random", 3, 1, &s_AiCommandFields[247] },
	{ 0x0091, "noop_0091", 2, 0, NULL },
	{ 0x0092, "set_hear_distance", 4, 1, &s_AiCommandFields[248] },
	{ 0x0093, "set_view_distance", 3, 1, &s_AiCommandFields[249] },
	{ 0x0094, "set_grenade_probability", 3, 1, &s_AiCommandFields[250] },
	{ 0x0095, "set_chr_num", 3, 1, &s_AiCommandFields[251] },
	{ 0x0096, "set_max_damage", 5, 2, &s_AiCommandFields[252] },
	{ 0x0097, "add_health", 4, 1, &s_AiCommandFields[254] },
	{ 0x0098, "set_reaction_speed", 3, 1, &s_AiCommandFields[255] },
	{ 0x0099, "set_recovery_speed", 3, 1, &s_AiCommandFields[256] },
	{ 0x009a, "set_accuracy", 3, 1, &s_AiCommandFields[257] },
	{ 0x009b, "set_flag", 7, 2, &s_AiCommandFields[258] },
	{ 0x009c, "unset_flag", 7, 2, &s_AiCommandFields[260] },
	{ 0x009d, "if_has_flag", 9, 4, &s_AiCommandFields[262] },
	{ 0x009e, "chr_set_flag", 8, 3, &s_AiCommandFields[266] },
	{ 0x009f, "chr_unset_flag", 8, 3, &s_AiCommandFields[269] },
	{ 0x00a0, "if_chr_has_flag", 9, 4, &s_AiCommandFields[272] },
	{ 0x00a1, "set_stage_flag", 6, 1, &s_AiCommandFields[276] },
	{ 0x00a2, "unset_stage_flag", 6, 1, &s_AiCommandFields[277] },
	{ 0x00a3, "if_stage_flag_eq", 8, 3, &s_AiCommandFields[278] },
	{ 0x00a4, "set_chrflag", 6, 1, &s_AiCommandFields[281] },
	{ 0x00a5, "unset_chrflag", 6, 1, &s_AiCommandFields[282] },
	{ 0x00a6, "if_has_chrflag", 7, 2, &s_AiCommandFields[283] },
	{ 0x00a7, "chr_set_chrflag", 7, 2, &s_AiCommandFields[285] },
	{ 0x00a8, "chr_unset_chrflag", 7, 2, &s_AiCommandFields[287] },
	{ 0x00a9, "if_chr_has_chrflag", 8, 3, &s_AiCommandFields[289] },
	{ 0x00aa, "set_obj_flag", 7, 2, &s_AiCommandFields[292] },
	{ 0x00ab, "unset_obj_flag", 7, 2, &s_AiCommandFields[294] },
	{ 0x00ac, "if_obj_has_flag", 8, 3, &s_AiCommandFields[296] },
	{ 0x00ad, "set_obj_flag_2", 7, 2, &s_AiCommandFields[299] },
	{ 0x00ae, "unset_obj_flag_2", 7, 2, &s_AiCommandFields[301] },
	{ 0x00af, "if_obj_has_flag_2", 8, 3, &s_AiCommandFields[303] },
	{ 0x00b0, "set_chr_preset", 3, 1, &s_AiCommandFields[306] },
	{ 0x00b1, "set_chr_target", 4, 2, &s_AiCommandFields[307] },
	{ 0x00b2, "set_pad_preset", 4, 1, &s_AiCommandFields[309] },
	{ 0x00b3, "chr_set_pad_preset", 5, 2, &s_AiCommandFields[310] },
	{ 0x00b4, "chr_copy_pad_preset", 4, 2, &s_AiCommandFields[312] },
	{ 0x00b5, "print", 0, 1, &s_AiCommandFields[314] },
	{ 0x00b6, "restart_timer", 2, 0, NULL },
	{ 0x00b7, "reset_timer", 2, 0, NULL },
	{ 0x00b8, "pause_timer", 2, 0, NULL },
	{ 0x00b9, "resume_timer", 2, 0, NULL },
	{ 0x00ba, "if_timer_stopped", 3, 1, &s_AiCommandFields[315] },
	{ 0x00bb, "if_timer_greater_than_random", 3, 1, &s_AiCommandFields[316] },
	{ 0x00bc, "if_timer_less_than", 6, 2, &s_AiCommandFields[317] },
	{ 0x00bd, "if_timer_greater_than", 6, 2, &s_AiCommandFields[319] },
	{ 0x00be, "show_countdown_timer", 2, 0, NULL },
	{ 0x00bf, "hide_countdown_timer", 2, 0, NULL },
	{ 0x00c0, "set_countdown_timer_value", 4, 1, &s_AiCommandFields[321] },
	{ 0x00c1, "stop_countdown_timer", 2, 0, NULL },
	{ 0x00c2, "start_countdown_timer", 2, 0, NULL },
	{ 0x00c3, "if_countdown_timer_stopped", 3, 1, &s_AiCommandFields[322] },
	{ 0x00c4, "if_countdown_timer_less_than", 5, 2, &s_AiCommandFields[323] },
	{ 0x00c5, "if_countdown_timer_greater_than", 5, 2, &s_AiCommandFields[325] },
	{ 0x00c6, "spawn_chr_at_pad", 13, 6, &s_AiCommandFields[327] },
	{ 0x00c7, "spawn_chr_at_chr", 12, 6, &s_AiCommandFields[333] },
	{ 0x00c8, "try_equip_weapon", 10, 4, &s_AiCommandFields[339] },
	{ 0x00c9, "try_equip_hat", 9, 3, &s_AiCommandFields[343] },
	{ 0x00ca, "duplicate_chr", 10, 4, &s_AiCommandFields[346] },
	{ 0x00cb, "show_hudmsg", 5, 2, &s_AiCommandFields[350] },
	{ 0x00cc, "show_hudmsg_top_middle", 6, 3, &s_AiCommandFields[352] },
	{ 0x00cd, "speak", 9, 5, &s_AiCommandFields[355] },
	{ 0x00ce, "play_sound", 5, 2, &s_AiCommandFields[360] },
	{ 0x00cf, "set_object_sound_playing", 6, 3, &s_AiCommandFields[362] },
	{ 0x00d0, "play_repeating_sound_from_pad", 7, 3, &s_AiCommandFields[365] },
	{ 0x00d1, "set_object_sound_volume", 7, 3, &s_AiCommandFields[368] },
	{ 0x00d2, "set_object_sound_volume_by_distance", 7, 3, &s_AiCommandFields[371] },
	{ 0x00d3, "audio_mute_channel", 3, 1, &s_AiCommandFields[374] },
	{ 0x00d4, "if_object_sound_volume_less_than", 6, 3, &s_AiCommandFields[375] },
	{ 0x00d5, "hovercar_begin_path", 3, 1, &s_AiCommandFields[378] },
	{ 0x00d6, "set_vehicle_speed", 6, 2, &s_AiCommandFields[379] },
	{ 0x00d7, "set_rotor_speed", 6, 2, &s_AiCommandFields[381] },
	{ 0x00d8, "noop_00d8", 3, 1, &s_AiCommandFields[383] },
	{ 0x00d9, "noop_00d9", 3, 1, &s_AiCommandFields[384] },
	{ 0x00da, "set_obj_image", 5, 3, &s_AiCommandFields[385] },
	{ 0x00db, "noop_00db", 3, 1, &s_AiCommandFields[388] },
	{ 0x00dc, "end_level", 2, 0, NULL },
	{ 0x00dd, "end_cutscene", 2, 0, NULL },
	{ 0x00de, "warp_jo_to_pad", 4, 1, &s_AiCommandFields[389] },
	{ 0x00df, "warp_jo_to_tag", 7, 3, &s_AiCommandFields[390] },
	{ 0x00e0, "revoke_control", 4, 2, &s_AiCommandFields[393] },
	{ 0x00e1, "grant_control", 3, 1, &s_AiCommandFields[395] },
	{ 0x00e2, "chr_move_to_pad", 7, 4, &s_AiCommandFields[396] },
	{ 0x00e3, "player_fade_in", 3, 1, &s_AiCommandFields[400] },
	{ 0x00e4, "players_fade_out", 3, 1, &s_AiCommandFields[401] },
	{ 0x00e5, "if_colour_fade_complete", 4, 2, &s_AiCommandFields[402] },
	{ 0x00e8, "set_door_open", 3, 1, &s_AiCommandFields[404] },
	{ 0x00e9, "chr_delete_weapon", 4, 2, &s_AiCommandFields[405] },
	{ 0x00ea, "if_num_players_less_than", 4, 2, &s_AiCommandFields[407] },
	{ 0x00eb, "if_chr_ammo_quantity_less_than", 6, 4, &s_AiCommandFields[409] },
	{ 0x00ec, "chr_draw_weapon", 4, 2, &s_AiCommandFields[413] },
	{ 0x00ed, "chr_draw_weapon_in_cutscene", 4, 2, &s_AiCommandFields[415] },
	{ 0x00ee, "set_player_force_speed", 5, 3, &s_AiCommandFields[417] },
	{ 0x00ef, "if_obj_in_room", 6, 3, &s_AiCommandFields[420] },
	{ 0x00f0, "ai_00f0", 3, 1, &s_AiCommandFields[423] },
	{ 0x00f1, "if_attacking", 3, 1, &s_AiCommandFields[424] },
	{ 0x00f2, "switch_to_alt_sky", 2, 0, NULL },
	{ 0x00f3, "chr_set_invincible", 3, 1, &s_AiCommandFields[425] },
	{ 0x00f4, "prepare_warp_orbit", 14, 6, &s_AiCommandFields[426] },
	{ 0x00f5, "begin_warp_latch", 2, 0, NULL },
	{ 0x00f6, "if_warp_latch_complete", 3, 1, &s_AiCommandFields[432] },
	{ 0x00f7, "if_all_objectives_complete", 3, 1, &s_AiCommandFields[433] },
	{ 0x00f8, "if_player_is_invincible", 4, 2, &s_AiCommandFields[434] },
	{ 0x00f9, "play_x_track", 5, 3, &s_AiCommandFields[436] },
	{ 0x00fa, "stop_x_track", 3, 1, &s_AiCommandFields[439] },
	{ 0x00fb, "chr_explosions", 3, 1, &s_AiCommandFields[440] },
	{ 0x00fc, "if_kill_count_greater_than", 4, 2, &s_AiCommandFields[441] },
	{ 0x00fd, "if_trigger_shot_list", 4, 2, &s_AiCommandFields[443] },
	{ 0x00fe, "kill_bond", 2, 0, NULL },
	{ 0x00ff, "be_surprised_surrender", 2, 0, NULL },
	{ 0x0100, "noop_0100", 3, 1, &s_AiCommandFields[445] },
	{ 0x0101, "noop_0101", 3, 1, &s_AiCommandFields[446] },
	{ 0x0102, "set_lights", 11, 8, &s_AiCommandFields[447] },
	{ 0x0103, "if_prop_preset_blocking_sight_to_target", 3, 1, &s_AiCommandFields[455] },
	{ 0x0104, "remove_object_at_prop_preset", 2, 0, NULL },
	{ 0x0105, "if_prop_preset_height_less_than", 5, 2, &s_AiCommandFields[456] },
	{ 0x0106, "set_target", 5, 3, &s_AiCommandFields[458] },
	{ 0x0107, "if_presets_target_is_not_my_target", 3, 1, &s_AiCommandFields[461] },
	{ 0x0108, "if_chr_target", 6, 4, &s_AiCommandFields[462] },
	{ 0x0109, "set_chr_preset_to_chr_near_self", 6, 3, &s_AiCommandFields[466] },
	{ 0x010a, "set_chr_preset_to_chr_near_pad", 8, 4, &s_AiCommandFields[469] },
	{ 0x010b, "chr_set_team", 4, 2, &s_AiCommandFields[473] },
	{ 0x010c, "if_compare_chr_presets_team", 4, 2, &s_AiCommandFields[475] },
	{ 0x010d, "noop_010d", 2, 0, NULL },
	{ 0x010e, "set_shield", 4, 1, &s_AiCommandFields[477] },
	{ 0x010f, "if_chr_shield_less_than", 6, 3, &s_AiCommandFields[478] },
	{ 0x0110, "if_chr_shield_greater_than", 6, 3, &s_AiCommandFields[481] },
	{ 0x0111, "set_camera_animation", 4, 1, &s_AiCommandFields[484] },
	{ 0x0112, "object_do_animation", 8, 4, &s_AiCommandFields[485] },
	{ 0x0113, "if_in_cutscene", 3, 1, &s_AiCommandFields[489] },
	{ 0x0114, "enable_chr", 3, 1, &s_AiCommandFields[490] },
	{ 0x0115, "disable_chr", 3, 1, &s_AiCommandFields[491] },
	{ 0x0116, "enable_obj", 3, 1, &s_AiCommandFields[492] },
	{ 0x0117, "disable_obj", 3, 1, &s_AiCommandFields[493] },
	{ 0x0118, "set_obj_flag_3", 7, 2, &s_AiCommandFields[494] },
	{ 0x0119, "unset_obj_flag_3", 7, 2, &s_AiCommandFields[496] },
	{ 0x011a, "if_obj_has_flag_3", 8, 3, &s_AiCommandFields[498] },
	{ 0x011b, "chr_set_hidden_flag", 7, 2, &s_AiCommandFields[501] },
	{ 0x011c, "chr_unset_hidden_flag", 7, 2, &s_AiCommandFields[503] },
	{ 0x011d, "if_chr_has_hidden_flag", 8, 3, &s_AiCommandFields[505] },
	{ 0x011e, "if_human", 4, 2, &s_AiCommandFields[508] },
	{ 0x011f, "if_skedar", 4, 2, &s_AiCommandFields[510] },
	{ 0x0120, "if_safety_2_less_than", 4, 2, &s_AiCommandFields[512] },
	{ 0x0121, "find_cover", 5, 2, &s_AiCommandFields[514] },
	{ 0x0122, "find_cover_within_dist", 9, 3, &s_AiCommandFields[516] },
	{ 0x0123, "find_cover_outside_dist", 9, 3, &s_AiCommandFields[519] },
	{ 0x0124, "go_to_cover", 3, 1, &s_AiCommandFields[522] },
	{ 0x0125, "check_cover_out_of_sight", 3, 1, &s_AiCommandFields[523] },
	{ 0x0126, "if_player_using_cmp_or_ar_34", 3, 1, &s_AiCommandFields[524] },
	{ 0x0127, "detect_enemy_on_same_floor", 3, 1, &s_AiCommandFields[525] },
	{ 0x0128, "detect_enemy", 4, 2, &s_AiCommandFields[526] },
	{ 0x0129, "if_safety_less_than", 4, 2, &s_AiCommandFields[528] },
	{ 0x012a, "if_target_moving_slowly", 4, 2, &s_AiCommandFields[530] },
	{ 0x012b, "if_target_moving_closer", 3, 1, &s_AiCommandFields[532] },
	{ 0x012c, "if_target_moving_away", 3, 1, &s_AiCommandFields[533] },
	{ 0x012f, "release_cover", 2, 0, NULL },
	{ 0x0130, "say_quip", 10, 8, &s_AiCommandFields[534] },
	{ 0x0131, "increase_squadron_alertness", 3, 1, &s_AiCommandFields[542] },
	{ 0x0132, "set_action", 4, 2, &s_AiCommandFields[543] },
	{ 0x0133, "set_team_orders", 4, 2, &s_AiCommandFields[545] },
	{ 0x0134, "if_orders", 5, 3, &s_AiCommandFields[547] },
	{ 0x0135, "if_has_orders", 3, 1, &s_AiCommandFields[550] },
	{ 0x0136, "retreat", 4, 2, &s_AiCommandFields[551] },
	{ 0x0137, "if_chr_in_squadron_doing_action", 4, 2, &s_AiCommandFields[553] },
	{ 0x0138, "if_channel_free", 4, 2, &s_AiCommandFields[555] },
	{ 0x0139, "ai_0139", 8, 3, &s_AiCommandFields[557] },
	{ 0x013a, "set_chr_preset_to_unalerted_teammate", 5, 3, &s_AiCommandFields[560] },
	{ 0x013b, "set_squadron", 3, 1, &s_AiCommandFields[563] },
	{ 0x013c, "face_cover", 3, 1, &s_AiCommandFields[564] },
	{ 0x013d, "if_dangerous_object_nearby", 4, 2, &s_AiCommandFields[565] },
	{ 0x013e, "ai_013e", 2, 0, NULL },
	{ 0x013f, "if_heli_weapons_armed", 3, 1, &s_AiCommandFields[567] },
	{ 0x0140, "if_hoverbot_next_step", 5, 3, &s_AiCommandFields[568] },
	{ 0x0141, "shuffle_investigation_terminals", 9, 7, &s_AiCommandFields[571] },
	{ 0x0142, "set_pad_preset_to_investigation_terminal", 4, 2, &s_AiCommandFields[578] },
	{ 0x0143, "heli_arm_weapons", 2, 0, NULL },
	{ 0x0144, "heli_unarm_weapons", 2, 0, NULL },
	{ 0x0145, "rebuild_teams", 2, 0, NULL },
	{ 0x0146, "rebuild_squadrons", 2, 0, NULL },
	{ 0x0147, "if_squadron_is_dead", 4, 2, &s_AiCommandFields[580] },
	{ 0x0148, "chr_set_listening", 4, 2, &s_AiCommandFields[582] },
	{ 0x0149, "if_chr_listening", 6, 4, &s_AiCommandFields[584] },
	{ 0x014a, "if_true", 6, 4, &s_AiCommandFields[588] },
	{ 0x014b, "if_not_listening", 3, 1, &s_AiCommandFields[592] },
	{ 0x0152, "if_num_chrs_in_squadron_greater_than", 5, 3, &s_AiCommandFields[593] },
	{ 0x0157, "set_tinted_glass_enabled", 3, 1, &s_AiCommandFields[596] },
	{ 0x015b, "play_track_isolated", 3, 1, &s_AiCommandFields[597] },
	{ 0x015c, "play_default_tracks", 2, 0, NULL },
	{ 0x0165, "if_chr_injured_target", 4, 2, &s_AiCommandFields[598] },
	{ 0x0166, "if_action", 4, 2, &s_AiCommandFields[600] },
	{ 0x0167, "hovercopter_fire_rocket", 3, 1, &s_AiCommandFields[602] },
	{ 0x0168, "if_shield_damaged", 4, 2, &s_AiCommandFields[603] },
	{ 0x0169, "if_natural_anim", 4, 2, &s_AiCommandFields[605] },
	{ 0x016a, "if_y", 7, 4, &s_AiCommandFields[607] },
	{ 0x016b, "play_repeating_sound_from_object", 10, 5, &s_AiCommandFields[611] },
	{ 0x016c, "noop_016c", 2, 0, NULL },
	{ 0x016d, "chr_adjust_motion_blur", 5, 3, &s_AiCommandFields[616] },
	{ 0x016e, "damage_chr_by_amount", 5, 3, &s_AiCommandFields[619] },
	{ 0x016f, "if_chr_has_no_gun", 5, 3, &s_AiCommandFields[622] },
	{ 0x0170, "do_gun_command", 4, 2, &s_AiCommandFields[625] },
	{ 0x0171, "if_distance_to_gun_less_than", 5, 2, &s_AiCommandFields[627] },
	{ 0x0172, "recover_gun", 4, 2, &s_AiCommandFields[629] },
	{ 0x0173, "chr_copy_properties", 4, 2, &s_AiCommandFields[631] },
	{ 0x0174, "if_cutscene_button_pressed", 3, 1, &s_AiCommandFields[633] },
	{ 0x0175, "reorient_for_cutscene_stop", 3, 1, &s_AiCommandFields[634] },
	{ 0x0176, "if_bot_respawning", 3, 1, &s_AiCommandFields[635] },
	{ 0x0177, "player_auto_walk", 9, 6, &s_AiCommandFields[636] },
	{ 0x0178, "if_player_auto_walk_finished", 4, 2, &s_AiCommandFields[642] },
	{ 0x0179, "play_sound_from_entity", 11, 6, &s_AiCommandFields[644] },
	{ 0x017a, "if_los_to_attack_target", 3, 1, &s_AiCommandFields[650] },
	{ 0x017b, "if_chr_knocked_out", 4, 2, &s_AiCommandFields[651] },
	{ 0x017c, "assign_sound", 5, 2, &s_AiCommandFields[653] },
	{ 0x017d, "play_cutscene_track", 3, 1, &s_AiCommandFields[655] },
	{ 0x017e, "stop_cutscene_track", 2, 0, NULL },
	{ 0x017f, "play_temporary_track", 3, 1, &s_AiCommandFields[656] },
	{ 0x0180, "stop_ambient_track", 2, 0, NULL },
	{ 0x0181, "if_player_looking_at_object", 5, 3, &s_AiCommandFields[657] },
	{ 0x0182, "punch_or_kick", 4, 2, &s_AiCommandFields[660] },
	{ 0x0183, "if_target_is_player", 3, 1, &s_AiCommandFields[662] },
	{ 0x0184, "try_attack_amount", 4, 2, &s_AiCommandFields[663] },
	{ 0x0185, "mp_init_simulants", 2, 0, NULL },
	{ 0x0186, "if_sound_timer", 6, 3, &s_AiCommandFields[665] },
	{ 0x0187, "set_target_to_eyespy_if_in_sight", 3, 1, &s_AiCommandFields[668] },
	{ 0x0188, "if_lift_stationary", 4, 2, &s_AiCommandFields[669] },
	{ 0x0189, "lift_go_to_stop", 4, 2, &s_AiCommandFields[671] },
	{ 0x018a, "if_lift_at_stop", 5, 3, &s_AiCommandFields[673] },
	{ 0x018b, "configure_rain", 3, 1, &s_AiCommandFields[676] },
	{ 0x018c, "chr_toggle_model_part", 4, 2, &s_AiCommandFields[677] },
	{ 0x018d, "activate_lift", 4, 2, &s_AiCommandFields[679] },
	{ 0x018e, "mini_skedar_try_pounce", 7, 4, &s_AiCommandFields[681] },
	{ 0x018f, "if_object_distance_to_pad_less_than", 8, 4, &s_AiCommandFields[685] },
	{ 0x0190, "set_savefile_flag", 3, 1, &s_AiCommandFields[689] },
	{ 0x0191, "unset_savefile_flag", 3, 1, &s_AiCommandFields[690] },
	{ 0x0192, "if_savefile_flag_is_set", 4, 2, &s_AiCommandFields[691] },
	{ 0x0193, "if_savefile_flag_is_unset", 4, 2, &s_AiCommandFields[693] },
	{ 0x019e, "if_obj_health_less_than", 6, 3, &s_AiCommandFields[695] },
	{ 0x019f, "set_obj_health", 5, 2, &s_AiCommandFields[698] },
	{ 0x01a0, "set_chr_special_death_animation", 4, 2, &s_AiCommandFields[700] },
	{ 0x01a1, "set_room_to_search", 2, 0, NULL },
	{ 0x01a2, "say_ci_staff_quip", 4, 2, &s_AiCommandFields[702] },
	{ 0x01a3, "do_preset_animation", 3, 1, &s_AiCommandFields[704] },
	{ 0x01a4, "show_hudmsg_middle", 6, 3, &s_AiCommandFields[705] },
	{ 0x01a5, "if_using_lift", 3, 1, &s_AiCommandFields[708] },
	{ 0x01a6, "if_target_y_difference_less_than", 4, 2, &s_AiCommandFields[709] },
	{ 0x01a7, "if_chr_not_talking", 4, 2, &s_AiCommandFields[711] },
	{ 0x01aa, "if_player_chr_portal_distance_less_than", 3, 1, &s_AiCommandFields[713] },
	{ 0x01ab, "if_num_knocked_out_chrs", 5, 3, &s_AiCommandFields[714] },
	{ 0x01ad, "release_object", 3, 1, &s_AiCommandFields[717] },
	{ 0x01ae, "clear_inventory", 3, 1, &s_AiCommandFields[718] },
	{ 0x01af, "chr_grab_object", 4, 2, &s_AiCommandFields[719] },
	{ 0x01b1, "shuffle_ruins_pillars", 18, 16, &s_AiCommandFields[721] },
	{ 0x01b2, "set_wind_speed", 3, 1, &s_AiCommandFields[737] },
	{ 0x01b3, "toggle_p1p2", 3, 1, &s_AiCommandFields[738] },
	{ 0x01b4, "if_chr_reposition_valid", 3, 1, &s_AiCommandFields[739] },
	{ 0x01b5, "chr_set_p1p2", 4, 2, &s_AiCommandFields[740] },
	{ 0x01b6, "configure_snow", 3, 1, &s_AiCommandFields[742] },
	{ 0x01b7, "chr_set_cloaked", 5, 3, &s_AiCommandFields[743] },
	{ 0x01b8, "set_autogun_target_team", 4, 2, &s_AiCommandFields[746] },
	{ 0x01b9, "shuffle_pelagic_switches", 2, 0, NULL },
	{ 0x01ba, "try_attack_lie", 7, 3, &s_AiCommandFields[748] },
	{ 0x01bb, "noop_01bb", 4, 2, &s_AiCommandFields[751] },
	{ 0x01bc, "if_pouncebits_eq", 4, 2, &s_AiCommandFields[753] },
	{ 0x01bd, "if_training_pc_holographed", 3, 1, &s_AiCommandFields[755] },
	{ 0x01be, "if_player_using_device", 5, 3, &s_AiCommandFields[756] },
	{ 0x01bf, "chr_begin_or_end_teleport", 5, 2, &s_AiCommandFields[759] },
	{ 0x01c0, "if_chr_teleport_full_white", 4, 2, &s_AiCommandFields[761] },
	{ 0x01c1, "set_punch_dodge_list", 4, 1, &s_AiCommandFields[763] },
	{ 0x01c2, "set_shooting_at_me_list", 4, 1, &s_AiCommandFields[764] },
	{ 0x01c3, "set_dark_room_list", 4, 1, &s_AiCommandFields[765] },
	{ 0x01c4, "set_player_dead_list", 4, 1, &s_AiCommandFields[766] },
	{ 0x01c5, "avoid", 2, 0, NULL },
	{ 0x01c6, "set_dodge_rating", 4, 2, &s_AiCommandFields[767] },
	{ 0x01c7, "set_unarmed_dodge_rating", 3, 1, &s_AiCommandFields[769] },
	{ 0x01c8, "title_init_mode", 3, 1, &s_AiCommandFields[770] },
	{ 0x01c9, "try_exit_title", 3, 1, &s_AiCommandFields[771] },
	{ 0x01ca, "chr_set_cutscene_weapon", 5, 3, &s_AiCommandFields[772] },
	{ 0x01cb, "fade_screen", 8, 2, &s_AiCommandFields[775] },
	{ 0x01cc, "if_fade_complete", 3, 1, &s_AiCommandFields[777] },
	{ 0x01cd, "set_chr_hudpiece_visible", 4, 2, &s_AiCommandFields[778] },
	{ 0x01ce, "set_passive_mode", 3, 1, &s_AiCommandFields[780] },
	{ 0x01cf, "chr_set_firing_in_cutscene", 4, 2, &s_AiCommandFields[781] },
	{ 0x01d0, "set_portal_flag", 5, 2, &s_AiCommandFields[783] },
	{ 0x01d1, "obj_set_model_part_visible", 5, 3, &s_AiCommandFields[785] },
	{ 0x01d2, "chr_emit_sparks", 3, 1, &s_AiCommandFields[788] },
	{ 0x01d3, "set_dr_caroll_images", 5, 3, &s_AiCommandFields[789] },
	{ 0x01d4, "set_room_flag", 6, 2, &s_AiCommandFields[792] },
	{ 0x01d5, "show_cutscene_chrs", 3, 1, &s_AiCommandFields[794] },
	{ 0x01d6, "configure_environment", 6, 3, &s_AiCommandFields[795] },
	{ 0x01d7, "if_distance_to_target_2_less_than", 5, 2, &s_AiCommandFields[798] },
	{ 0x01d8, "if_distance_to_target_2_greater_than", 5, 2, &s_AiCommandFields[800] },
	{ 0x01d9, "play_sound_from_prop", 11, 7, &s_AiCommandFields[802] },
	{ 0x01da, "play_temporary_primary_track", 3, 1, &s_AiCommandFields[809] },
	{ 0x01db, "chr_kill", 3, 1, &s_AiCommandFields[810] },
	{ 0x01dc, "remove_weapon_from_inventory", 3, 1, &s_AiCommandFields[811] },
	{ 0x01dd, "if_music_event_queue_is_empty", 4, 2, &s_AiCommandFields[812] },
	{ 0x01de, "if_coop_mode", 3, 1, &s_AiCommandFields[814] },
	{ 0x01df, "if_chr_same_floor_distance_to_pad_less_than", 8, 4, &s_AiCommandFields[815] },
	{ 0x01e0, "remove_references_to_chr", 2, 0, NULL },
};

#define AI_COMMAND_COUNT (sizeof(s_AiCommands) / sizeof(s_AiCommands[0]))

const ai_command_schema_t *aiCommandSchemaFind(uint16_t opcode)
{
	uint32_t lo = 0;
	uint32_t hi = (uint32_t)AI_COMMAND_COUNT;

	while (lo < hi) {
		const uint32_t mid = lo + (hi - lo) / 2;

		if (s_AiCommands[mid].opcode == opcode) {
			return &s_AiCommands[mid];
		}
		if (s_AiCommands[mid].opcode < opcode) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return NULL;
}

uint32_t aiCommandSchemaCount(void)
{
	return (uint32_t)AI_COMMAND_COUNT;
}

const ai_command_schema_t *aiCommandSchemaAt(uint32_t index)
{
	return index < AI_COMMAND_COUNT ? &s_AiCommands[index] : NULL;
}

const ai_command_field_t *aiCommandSchemaField(const ai_command_schema_t *schema,
	const char *name)
{
	if (!schema || !name) {
		return NULL;
	}
	for (uint32_t i = 0; i < schema->field_count; i++) {
		if (strcmp(schema->fields[i].name, name) == 0) {
			return &schema->fields[i];
		}
	}
	return NULL;
}

const char *aiOperandKindName(ai_operand_kind_t kind)
{
	return (unsigned)kind < AI_OPERAND_KIND_COUNT ? s_AiOperandKindNames[kind] : NULL;
}

int aiOperandKindFromName(const char *name)
{
	if (!name) {
		return -1;
	}
	for (int i = 0; i < AI_OPERAND_KIND_COUNT; i++) {
		if (strcmp(s_AiOperandKindNames[i], name) == 0) {
			return i;
		}
	}
	return -1;
}

static int s_fieldFits(const ai_command_field_t *field, uint32_t operand_len)
{
	return field && field->width >= 1 && field->width <= 4 && field->offset >= 2
		&& (uint32_t)(field->offset - 2) + field->width <= operand_len;
}

void aiCommandFieldRange(const ai_command_field_t *field, int64_t *out_min,
	int64_t *out_max)
{
	const unsigned bits = field && field->width >= 1 && field->width <= 4
		? 8u * field->width : 0u;
	int64_t min = 0;
	int64_t max = 0;

	if (bits) {
		if (field->is_signed) {
			min = -((int64_t)1 << (bits - 1));
			max = ((int64_t)1 << (bits - 1)) - 1;
		} else {
			max = ((int64_t)1 << bits) - 1;
		}
	}
	if (out_min) {
		*out_min = min;
	}
	if (out_max) {
		*out_max = max;
	}
}

int aiCommandFieldRead(const ai_command_field_t *field, const uint8_t *operands,
	uint32_t operand_len, int64_t *out_value)
{
	uint64_t raw = 0;
	const uint8_t *at;

	if (!operands || !out_value || !s_fieldFits(field, operand_len)) {
		return 0;
	}
	at = operands + (field->offset - 2);
	for (uint32_t i = 0; i < field->width; i++) {
		raw = (raw << 8) | at[i];
	}
	if (field->is_signed && (raw & ((uint64_t)1 << (8u * field->width - 1u)))) {
		*out_value = (int64_t)raw - ((int64_t)1 << (8u * field->width));
	} else {
		*out_value = (int64_t)raw;
	}
	return 1;
}

int aiCommandFieldWrite(const ai_command_field_t *field, uint8_t *operands,
	uint32_t operand_len, int64_t value)
{
	int64_t min;
	int64_t max;
	uint64_t raw;
	uint8_t *at;

	if (!operands || !s_fieldFits(field, operand_len)) {
		return 0;
	}
	aiCommandFieldRange(field, &min, &max);
	if (value < min || value > max) {
		return 0;
	}
	raw = (uint64_t)value;
	at = operands + (field->offset - 2);
	for (uint32_t i = field->width; i > 0; i--) {
		at[i - 1] = (uint8_t)(raw & 0xffu);
		raw >>= 8;
	}
	return 1;
}

/* ---------------------------------------------------------------------
 * Slice 2: arguments as JSON (ai-typed-operands.md).
 */

static int s_append(char *out, uint32_t out_size, uint32_t *len, const char *fmt, ...)
{
	va_list args;
	int written;

	if (*len >= out_size) {
		return 0;
	}
	va_start(args, fmt);
	written = vsnprintf(out + *len, out_size - *len, fmt, args);
	va_end(args);
	if (written < 0 || (uint32_t)written >= out_size - *len) {
		return 0;
	}
	*len += (uint32_t)written;
	return 1;
}

int aiOperandKindIsCatalog(ai_operand_kind_t kind)
{
	return kind == AI_OPERAND_MODEL || kind == AI_OPERAND_WEAPON || kind == AI_OPERAND_BODY
		|| kind == AI_OPERAND_HEAD || kind == AI_OPERAND_ANIM || kind == AI_OPERAND_SOUND;
}

void aiSoundRefSplit(int64_t value, int64_t *out_asset, int *out_priority,
	int *out_response)
{
	const uint32_t bits = (uint32_t)value & 0xffffu;
	int64_t asset;
	int priority = 0;
	int response = 0;

	if (bits & AI_SOUND_REF_CONFIGURED) {
		asset = bits;
	} else {
		priority = (int)((bits >> 11) & 3u);
		response = (int)((bits >> 13) & 3u);
		asset = (int64_t)(bits & AI_SOUND_REF_ID_MASK) | (priority ? AI_SOUND_REF_MP3 : 0);
	}
	if (out_asset) {
		*out_asset = asset;
	}
	if (out_priority) {
		*out_priority = priority;
	}
	if (out_response) {
		*out_response = response;
	}
}

/* A catalog kind's "none": the field's value with every bit set. */
static int64_t s_noneValue(const ai_command_field_t *field)
{
	if (field->is_signed) {
		return -1;
	}
	return field->width >= 4 ? (int64_t)0xffffffffu : ((int64_t)1 << (8 * field->width)) - 1;
}

/* A catalog id as the writer names it: only id characters, so it needs no
 * escaping in JSON. */
static int s_isCatalogIdText(const char *id)
{
	if (!id || !id[0]) {
		return 0;
	}
	for (const char *c = id; *c; c++) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')
				|| *c == '_' || *c == ':' || *c == '.' || *c == '-')) {
			return 0;
		}
	}
	return 1;
}

int aiCommandArgsToJson(const ai_command_schema_t *schema, const uint8_t *operands,
	uint32_t operand_len, char *out, uint32_t out_size)
{
	return aiCommandArgsToJsonRefs(schema, operands, operand_len, out, out_size, NULL, NULL);
}

int aiCommandArgsToJsonRefs(const ai_command_schema_t *schema, const uint8_t *operands,
	uint32_t operand_len, char *out, uint32_t out_size, ai_command_ref_namer_fn namer,
	void *user)
{
	uint32_t len = 0;
	int first = 1;

	if (!schema || !out || out_size == 0 || (operand_len && !operands)) {
		return -1;
	}
	out[0] = '\0';
	if (schema->length == 0) {
		/* print: the text up to its only NUL. */
		if (operand_len == 0 || operands[operand_len - 1] != 0
				|| memchr(operands, 0, operand_len - 1)) {
			return -1;
		}
		if (!s_append(out, out_size, &len, "{\"%s\": \"", schema->fields[0].name)) {
			return -1;
		}
		for (uint32_t i = 0; i + 1 < operand_len; i++) {
			const uint8_t b = operands[i];
			const int ok = b == '"' || b == '\\'
				? s_append(out, out_size, &len, "\\%c", (char)b)
				: b < 0x20 || b >= 0x7f
				? s_append(out, out_size, &len, "\\u%04x", (unsigned)b)
				: s_append(out, out_size, &len, "%c", (char)b);
			if (!ok) {
				return -1;
			}
		}
		return s_append(out, out_size, &len, "\"}") ? (int)len : -1;
	}
	if (operand_len != (uint32_t)schema->length - 2u
			|| !s_append(out, out_size, &len, "{")) {
		return -1;
	}
	for (uint32_t i = 0; i < schema->field_count; i++) {
		const ai_command_field_t *field = &schema->fields[i];
		int64_t value = 0;
		int ok;

		if (!aiCommandFieldRead(field, operands, operand_len, &value)) {
			return -1;
		}
		if (field->kind == AI_OPERAND_UNUSED && value == 0) {
			continue;
		}
		if (namer && aiOperandKindIsCatalog((ai_operand_kind_t)field->kind)) {
			int64_t asset = value;
			int priority = 0;
			int response = 0;
			const char *id;

			if (field->kind == AI_OPERAND_SOUND) {
				aiSoundRefSplit(value, &asset, &priority, &response);
			}
			id = namer((ai_operand_kind_t)field->kind, asset, user);
			if (s_isCatalogIdText(id)) {
				if (!s_append(out, out_size, &len, "%s\"%s\": \"%s\"", first ? "" : ", ",
						field->name, id)
						|| (priority && !s_append(out, out_size, &len, ", \"%s_priority\": %d",
							field->name, priority))
						|| (response && !s_append(out, out_size, &len, ", \"%s_response\": %d",
							field->name, response))) {
					return -1;
				}
				first = 0;
				continue;
			}
			/* Slice 3b: no asset by number. "none" is null; any other value
			 * the catalog cannot name fails the writer. */
			if (value != s_noneValue(field)
					|| !s_append(out, out_size, &len, "%s\"%s\": null", first ? "" : ", ",
						field->name)) {
				return -1;
			}
			first = 0;
			continue;
		}
		if (field->kind == AI_OPERAND_BOOL && (value == 0 || value == 1)) {
			ok = s_append(out, out_size, &len, "%s\"%s\": %s", first ? "" : ", ",
				field->name, value ? "true" : "false");
		} else {
			ok = s_append(out, out_size, &len, "%s\"%s\": %" PRId64, first ? "" : ", ",
				field->name, value);
		}
		if (!ok) {
			return -1;
		}
		first = 0;
	}
	return s_append(out, out_size, &len, "}") ? (int)len : -1;
}

static int s_fail(char *error, uint32_t error_size, const char *fmt, ...)
{
	va_list args;

	if (error && error_size) {
		va_start(args, fmt);
		vsnprintf(error, error_size, fmt, args);
		va_end(args);
	}
	return -1;
}

static const char *s_skipSpace(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
		p++;
	}
	return p;
}

/* A JSON string at p (its opening quote) into out (NUL-terminated, at most
 * out_size bytes with the NUL; \uXXXX only below 0x100). Returns the byte
 * after the closing quote, or NULL. *out_len excludes the NUL. */
static const char *s_readString(const char *p, const char *end, uint8_t *out,
	uint32_t out_size, uint32_t *out_len)
{
	uint32_t len = 0;

	if (p >= end || *p != '"') {
		return NULL;
	}
	p++;
	while (p < end && *p != '"') {
		uint32_t c = (uint8_t)*p++;

		if (c == '\\') {
			if (p >= end) {
				return NULL;
			}
			c = (uint8_t)*p++;
			if (c == 'u') {
				uint32_t value = 0;

				if (end - p < 4) {
					return NULL;
				}
				for (int i = 0; i < 4; i++) {
					const char h = *p++;

					value <<= 4;
					if (h >= '0' && h <= '9') {
						value |= (uint32_t)(h - '0');
					} else if (h >= 'a' && h <= 'f') {
						value |= (uint32_t)(h - 'a' + 10);
					} else if (h >= 'A' && h <= 'F') {
						value |= (uint32_t)(h - 'A' + 10);
					} else {
						return NULL;
					}
				}
				if (value == 0 || value > 0xff) {
					return NULL;
				}
				c = value;
			} else if (c == 'n') {
				c = '\n';
			} else if (c == 't') {
				c = '\t';
			} else if (c != '"' && c != '\\' && c != '/') {
				return NULL;
			}
		} else if (c < 0x20) {
			return NULL;
		}
		if (len + 1 >= out_size) {
			return NULL;
		}
		out[len++] = (uint8_t)c;
	}
	if (p >= end) {
		return NULL;
	}
	out[len] = 0;
	*out_len = len;
	return p + 1;
}

/* A JSON whole number at p (at most 18 digits). Returns the byte after it,
 * or NULL. */
static const char *s_readWhole(const char *p, const char *end, int64_t *out)
{
	int64_t value = 0;
	int negative = 0;
	int digits = 0;

	if (p < end && *p == '-') {
		negative = 1;
		p++;
	}
	while (p < end && *p >= '0' && *p <= '9') {
		if (digits >= 18) {
			return NULL;
		}
		value = value * 10 + (*p - '0');
		digits++;
		p++;
	}
	if (!digits) {
		return NULL;
	}
	*out = negative ? -value : value;
	return p;
}

/* Slice 3b: a sound field's value from its asset and its playback values
 * (-1 when an argument does not give one). Returns 0, or -1 with the
 * reason. */
static int s_soundRefJoin(const ai_command_schema_t *schema, const ai_command_field_t *field,
	int64_t asset, int priority, int response, int64_t *out, char *error, uint32_t error_size)
{
	uint32_t bits;

	if (asset < 0 || asset > 0xffff) {
		return s_fail(error, error_size, "%s argument '%s' names no sound", schema->name,
			field->name);
	}
	if (asset & AI_SOUND_REF_CONFIGURED) {
		if (priority >= 0 || response >= 0) {
			return s_fail(error, error_size, "%s argument '%s' is a configured sound, which"
				" takes no '%s_priority' or '%s_response'", schema->name, field->name,
				field->name, field->name);
		}
		bits = (uint32_t)asset;
	} else if (asset & AI_SOUND_REF_MP3) {
		if (priority < 1) {
			return s_fail(error, error_size, "%s argument '%s' is an MP3 voice line, which"
				" needs '%s_priority' (1 to 3)", schema->name, field->name, field->name);
		}
		bits = ((uint32_t)asset & AI_SOUND_REF_ID_MASK) | ((uint32_t)priority << 11);
	} else {
		if (priority >= 0) {
			return s_fail(error, error_size, "%s argument '%s' is a sample; '%s_priority' is"
				" for an MP3 voice line", schema->name, field->name, field->name);
		}
		bits = (uint32_t)asset;
	}
	if (response > 0) {
		bits |= (uint32_t)response << 13;
	}
	*out = field->is_signed && (bits & 0x8000u) ? (int64_t)bits - 0x10000 : (int64_t)bits;
	return 0;
}

/* One argument's value at *pp (after its key): checks it once, encodes it
 * into out and moves *pp past it. A sound named by catalog id is not
 * written here: its asset goes to *sound_asset, joined with its playback
 * values once every argument is read. Returns 0, or -1 with the reason. */
static int s_encodeField(const ai_command_schema_t *schema, const ai_command_field_t *field,
	const char **pp, const char *end, uint8_t *out, uint32_t out_size, uint32_t *count,
	ai_command_ref_resolver_fn resolver, void *user, int64_t *sound_asset, uint8_t *seen,
	char *error, uint32_t error_size)
{
	const char *p = *pp;
	const int catalog = aiOperandKindIsCatalog((ai_operand_kind_t)field->kind);
	int64_t value = 0;

	if (*seen) {
		return s_fail(error, error_size, "%s argument '%s' appears twice", schema->name,
			field->name);
	}
	*seen = 1;
	p = s_skipSpace(p, end);
	if (p >= end || *p != ':') {
		return s_fail(error, error_size, "%s argument '%s' has no value", schema->name,
			field->name);
	}
	p = s_skipSpace(p + 1, end);
	if (field->kind == AI_OPERAND_STRING) {
		uint32_t text_len = 0;

		if (out_size < 1) {
			return s_fail(error, error_size, "no room for %s's text", schema->name);
		}
		p = s_readString(p, end, out, out_size, &text_len);
		if (!p) {
			return s_fail(error, error_size, "%s argument '%s' is not a text it can hold",
				schema->name, field->name);
		}
		*count = text_len + 1u;
		*pp = p;
		return 0;
	}
	if (catalog && end - p >= 4 && strncmp(p, "null", 4) == 0) {
		value = s_noneValue(field);
		p += 4;
	} else if (catalog && p < end && *p == '"') {
		uint8_t id[96];
		uint32_t id_len = 0;

		p = s_readString(p, end, id, sizeof(id), &id_len);
		if (!p || !id_len) {
			return s_fail(error, error_size, "%s argument '%s' is not a catalog id",
				schema->name, field->name);
		}
		if (!resolver) {
			return s_fail(error, error_size, "%s argument '%s' names '%s'; this reader"
				" has no catalog", schema->name, field->name, (const char *)id);
		}
		if (!resolver((ai_operand_kind_t)field->kind, (const char *)id, &value, user)) {
			return s_fail(error, error_size, "%s argument '%s' names '%s', which is no %s"
				" in the catalog", schema->name, field->name, (const char *)id,
				aiOperandKindName((ai_operand_kind_t)field->kind));
		}
		if (field->kind == AI_OPERAND_SOUND) {
			*sound_asset = value;
			*pp = p;
			return 0;
		}
	} else if (field->kind == AI_OPERAND_BOOL && end - p >= 4 && strncmp(p, "true", 4) == 0) {
		value = 1;
		p += 4;
	} else if (field->kind == AI_OPERAND_BOOL && end - p >= 5 && strncmp(p, "false", 5) == 0) {
		value = 0;
		p += 5;
	} else {
		p = s_readWhole(p, end, &value);
		if (!p) {
			return s_fail(error, error_size, "%s argument '%s' must be %s", schema->name,
				field->name, field->kind == AI_OPERAND_BOOL ? "true or false"
				: catalog ? "a catalog id or null" : "a whole number");
		}
		if (catalog && resolver) {
			/* Slice 3b: with a catalog, an asset is named by its id. */
			return s_fail(error, error_size, "%s argument '%s' gives a %s by number; name it"
				" by catalog id", schema->name, field->name,
				aiOperandKindName((ai_operand_kind_t)field->kind));
		}
	}
	if (!aiCommandFieldWrite(field, out, *count, value)) {
		return s_fail(error, error_size, "%s argument '%s' = %" PRId64
			" is outside its %u-byte %s range", schema->name, field->name, value,
			(unsigned)field->width, field->is_signed ? "signed" : "unsigned");
	}
	*pp = p;
	return 0;
}

int aiCommandArgsEncode(const ai_command_schema_t *schema, const char *json,
	uint32_t json_len, uint8_t *out, uint32_t out_size, char *error,
	uint32_t error_size)
{
	return aiCommandArgsEncodeRefs(schema, json, json_len, out, out_size, NULL, NULL,
		error, error_size);
}

int aiCommandArgsEncodeRefs(const ai_command_schema_t *schema, const char *json,
	uint32_t json_len, uint8_t *out, uint32_t out_size, ai_command_ref_resolver_fn resolver,
	void *user, char *error, uint32_t error_size)
{
	const char *end;
	const char *p;
	uint8_t seen[64];
	/* Slice 3b: a sound named by catalog id, and its playback values. */
	int64_t sound_asset[64];
	int sound_priority[64];
	int sound_response[64];
	uint32_t count;

	if (error && error_size) {
		error[0] = '\0';
	}
	if (!schema || !json || !out) {
		return s_fail(error, error_size, "no arguments");
	}
	if (schema->field_count > sizeof(seen)) {
		return s_fail(error, error_size, "%s has too many fields", schema->name);
	}
	memset(seen, 0, sizeof(seen));
	for (uint32_t i = 0; i < sizeof(seen); i++) {
		sound_asset[i] = -1;
		sound_priority[i] = -1;
		sound_response[i] = -1;
	}
	count = schema->length ? (uint32_t)schema->length - 2u : 0u;
	if (count > out_size) {
		return s_fail(error, error_size, "%s needs %u operand bytes", schema->name,
			(unsigned)count);
	}
	memset(out, 0, count);
	end = json + json_len;
	p = s_skipSpace(json, end);
	if (p >= end || *p != '{') {
		return s_fail(error, error_size, "%s arguments are not an object", schema->name);
	}
	p = s_skipSpace(p + 1, end);
	if (p < end && *p == '}') {
		p++;
	} else {
		for (;;) {
			uint8_t key[64];
			uint32_t key_len = 0;
			const ai_command_field_t *field = NULL;
			uint32_t index = 0;
			int playback = 0; /* 1: "<sound>_priority", 2: "<sound>_response" */

			p = s_readString(p, end, key, sizeof(key), &key_len);
			if (!p) {
				return s_fail(error, error_size, "%s has a bad argument name", schema->name);
			}
			for (index = 0; index < schema->field_count; index++) {
				if (strcmp(schema->fields[index].name, (const char *)key) == 0) {
					field = &schema->fields[index];
					break;
				}
			}
			if (!field) {
				for (index = 0; index < schema->field_count; index++) {
					const ai_command_field_t *sound = &schema->fields[index];
					const size_t name_len = strlen(sound->name);

					if (sound->kind != AI_OPERAND_SOUND
							|| strncmp((const char *)key, sound->name, name_len) != 0) {
						continue;
					}
					playback = strcmp((const char *)key + name_len, "_priority") == 0 ? 1
						: strcmp((const char *)key + name_len, "_response") == 0 ? 2 : 0;
					if (playback) {
						field = sound;
						break;
					}
				}
			}
			if (!field) {
				return s_fail(error, error_size, "%s has no argument '%s'", schema->name,
					(const char *)key);
			}
			if (playback) {
				int *slot = playback == 1 ? &sound_priority[index] : &sound_response[index];
				int64_t value = 0;

				if (*slot >= 0) {
					return s_fail(error, error_size, "%s argument '%s' appears twice",
						schema->name, (const char *)key);
				}
				p = s_skipSpace(p, end);
				if (p >= end || *p != ':') {
					return s_fail(error, error_size, "%s argument '%s' has no value",
						schema->name, (const char *)key);
				}
				p = s_readWhole(s_skipSpace(p + 1, end), end, &value);
				if (!p || value < (playback == 1 ? 1 : 0) || value > 3) {
					return s_fail(error, error_size, "%s argument '%s' must be a whole number"
						" from %d to 3", schema->name, (const char *)key, playback == 1 ? 1 : 0);
				}
				*slot = (int)value;
			} else if (s_encodeField(schema, field, &p, end, out, out_size, &count, resolver,
					user, &sound_asset[index], &seen[index], error, error_size) < 0) {
				return -1;
			}
			p = s_skipSpace(p, end);
			if (p < end && *p == ',') {
				p = s_skipSpace(p + 1, end);
				continue;
			}
			if (p < end && *p == '}') {
				p++;
				break;
			}
			return s_fail(error, error_size, "%s arguments are not a closed object",
				schema->name);
		}
	}
	if (s_skipSpace(p, end) != end) {
		return s_fail(error, error_size, "%s arguments have text after the object",
			schema->name);
	}
	for (uint32_t i = 0; i < schema->field_count; i++) {
		const ai_command_field_t *field = &schema->fields[i];

		if (!seen[i] && field->kind != AI_OPERAND_UNUSED) {
			return s_fail(error, error_size, "%s is missing argument '%s'", schema->name,
				field->name);
		}
		if (field->kind != AI_OPERAND_SOUND) {
			continue;
		}
		if (sound_asset[i] >= 0) {
			int64_t value = 0;

			if (s_soundRefJoin(schema, field, sound_asset[i], sound_priority[i],
					sound_response[i], &value, error, error_size) < 0) {
				return -1;
			}
			if (!aiCommandFieldWrite(field, out, count, value)) {
				return s_fail(error, error_size, "%s argument '%s' = %" PRId64
					" is outside its %u-byte %s range", schema->name, field->name, value,
					(unsigned)field->width, field->is_signed ? "signed" : "unsigned");
			}
		} else if (sound_priority[i] >= 0 || sound_response[i] >= 0) {
			return s_fail(error, error_size, "%s arguments '%s_priority' and '%s_response'"
				" go with a sound named by catalog id", schema->name, field->name, field->name);
		}
	}
	return (int)count;
}
