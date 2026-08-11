/**
 * weapon_graph_runtime.h -- validator and deterministic IR compiler for
 * graph-authored weapon, projectile, and entity behavior.
 */
#ifndef _IN_WEAPON_GRAPH_RUNTIME_H
#define _IN_WEAPON_GRAPH_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEAPON_GRAPH_IR_MAX_NODES   64
#define WEAPON_GRAPH_IR_MAX_EDGES   128
#define WEAPON_GRAPH_IR_MAX_PARAMS  512
#define WEAPON_GRAPH_IR_ID_LEN      64
#define WEAPON_GRAPH_IR_KEY_LEN     64
#define WEAPON_GRAPH_IR_VALUE_LEN   160

typedef enum weapon_graph_opcode {
	WEAPON_GRAPH_OP_INVALID = 0,
	WEAPON_GRAPH_OP_FUNCTION_EMPTY = 1,

	WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED = 100,
	WEAPON_GRAPH_OP_EVENT_TRIGGER_HELD,
	WEAPON_GRAPH_OP_EVENT_TRIGGER_RELEASED,
	WEAPON_GRAPH_OP_GATE_AMMO_AVAILABLE,
	WEAPON_GRAPH_OP_GATE_COOLDOWN_READY,
	WEAPON_GRAPH_OP_GATE_TARGET_LOCK,
	WEAPON_GRAPH_OP_AMMO_CONSUME,
	WEAPON_GRAPH_OP_AMMO_RESERVE_TRANSFER,
	WEAPON_GRAPH_OP_FIRE_HITSCAN,
	WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE,
	WEAPON_GRAPH_OP_FIRE_BURST,
	WEAPON_GRAPH_OP_FIRE_CHARGE_RELEASE,
	WEAPON_GRAPH_OP_FIRE_BEAM_TICK,
	WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE,
	WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL,
	WEAPON_GRAPH_OP_MELEE_STRIKE,
	WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR,
	WEAPON_GRAPH_OP_SPECIAL_COMBAT_BOOST,
	WEAPON_GRAPH_OP_SPECIAL_WEAPON_STATE,
	WEAPON_GRAPH_OP_DEVICE_ACTIVATE,
	WEAPON_GRAPH_OP_PRESENTATION_WEAPON_VISIBILITY,
	WEAPON_GRAPH_OP_PRESENTATION_RETICLE_OVERLAY_CAMERA,

	WEAPON_GRAPH_OP_PROJECTILE_SPAWN_STATE = 300,
	WEAPON_GRAPH_OP_PROJECTILE_MOTION,
	WEAPON_GRAPH_OP_PROJECTILE_TRAJECTORY_CORRECTION,
	WEAPON_GRAPH_OP_PROJECTILE_HOMING,
	WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE,
	WEAPON_GRAPH_OP_PROJECTILE_WALL_HUGGER,
	WEAPON_GRAPH_OP_PROJECTILE_STICKY_ATTACH,
	WEAPON_GRAPH_OP_PROJECTILE_BOUNCE_SLIDE,
	WEAPON_GRAPH_OP_PROJECTILE_TIMER,
	WEAPON_GRAPH_OP_PROJECTILE_IMPACT,
	WEAPON_GRAPH_OP_PROJECTILE_TRAIL,
	WEAPON_GRAPH_OP_PROJECTILE_TRANSITION_TO_ENTITY,
	WEAPON_GRAPH_OP_PROJECTILE_PICKUP_RECOVER,

	WEAPON_GRAPH_OP_ENTITY_ARMED_EXPLOSIVE = 500,
	WEAPON_GRAPH_OP_ENTITY_PROXY_TRIGGER,
	WEAPON_GRAPH_OP_ENTITY_REMOTE_DETONATABLE,
	WEAPON_GRAPH_OP_ENTITY_TIMED_DETONATABLE,
	WEAPON_GRAPH_OP_ENTITY_NBOMB_STORM,
	WEAPON_GRAPH_OP_ENTITY_AUTOGUN,
	WEAPON_GRAPH_OP_ENTITY_STICKY_DEVICE,
	WEAPON_GRAPH_OP_ENTITY_OWNER_CLEANUP,
	WEAPON_GRAPH_OP_ENTITY_INTERACTION,

	/* c3849 Unit 8 (.pdeffect): the 700 range is the effect-graph module
	 * family. tint..particle are presentation kinds (gameplay-inert, recorded
	 * for the renderer slice); explosion/spark/smoke are the gameplay kinds
	 * whose executors ARE the OG explosion/spark/smoke machinery (closure
	 * rule - no new particle or render systems). */
	WEAPON_GRAPH_OP_EFFECT_TINT = 700,
	WEAPON_GRAPH_OP_EFFECT_GLOW,
	WEAPON_GRAPH_OP_EFFECT_SHIMMER,
	WEAPON_GRAPH_OP_EFFECT_DARKEN,
	WEAPON_GRAPH_OP_EFFECT_SCREEN,
	WEAPON_GRAPH_OP_EFFECT_PARTICLE,
	WEAPON_GRAPH_OP_EFFECT_EXPLOSION,
	WEAPON_GRAPH_OP_EFFECT_SPARK,
	WEAPON_GRAPH_OP_EFFECT_SMOKE,

	/* Public .pdprop behavior graph modules. These deliberately describe
	 * only operations that have production Forge runtime executors. */
	WEAPON_GRAPH_OP_PROP_EVENT_SPAWN = 900,
	WEAPON_GRAPH_OP_PROP_EVENT_TICK,
	WEAPON_GRAPH_OP_PROP_CONDITION_ENABLED,
	WEAPON_GRAPH_OP_PROP_ACTION_SET_ENABLED,
	WEAPON_GRAPH_OP_PROP_ACTION_SET_HEALTH,
	WEAPON_GRAPH_OP_PROP_ACTION_SET_COLLISION,
	WEAPON_GRAPH_OP_PROP_ACTION_SET_CHANNEL,
} weapon_graph_opcode_e;

typedef struct weapon_graph_parity_module {
	weapon_graph_opcode_e opcode;
	const char *module_name;
	const char *behavior_family;
	const char *legacy_backend;
	const char *parity_scope;
} weapon_graph_parity_module_t;

typedef enum weapon_graph_param_type {
	WEAPON_GRAPH_PARAM_NULL = 0,
	WEAPON_GRAPH_PARAM_STRING,
	WEAPON_GRAPH_PARAM_INT,
	WEAPON_GRAPH_PARAM_FLOAT,
	WEAPON_GRAPH_PARAM_BOOL,
	WEAPON_GRAPH_PARAM_OBJECT,
	WEAPON_GRAPH_PARAM_ARRAY,
} weapon_graph_param_type_e;

typedef struct weapon_graph_ir_param {
	char key[WEAPON_GRAPH_IR_KEY_LEN];
	weapon_graph_param_type_e type;
	s32 i_value;
	f32 f_value;
	s32 b_value;
	char value[WEAPON_GRAPH_IR_VALUE_LEN];
} weapon_graph_ir_param_t;

/* c3849 Wave 5f Unit 2: bounded per-weapon tunables store. settings.json /
 * variables.json (and presentation.json sight/zoom_fov keys, which fold into
 * the same defaults layer) parse into typed key rows that reuse the
 * weapon_graph_ir_param_t shape plus an explicit unit column. Base archives
 * author zero tunables, so the layer is empty for every base weapon. */
#define WEAPON_GRAPH_SETTINGS_MAX      32
#define WEAPON_GRAPH_SETTINGS_UNIT_LEN 24

typedef struct weapon_graph_setting_entry {
	char key[WEAPON_GRAPH_IR_KEY_LEN];
	weapon_graph_param_type_e type;
	s32 i_value;
	f32 f_value;
	s32 b_value;
	char value[WEAPON_GRAPH_IR_VALUE_LEN];
	char unit[WEAPON_GRAPH_SETTINGS_UNIT_LEN];
} weapon_graph_setting_entry_t;

typedef struct weapon_graph_weapon_settings {
	s32 valid;
	/* Public presentation binding. Empty selects the native sight renderer;
	 * otherwise this is an authoritative ASSET_UI catalog ID. */
	char reticle_ref[CATALOG_ID_LEN];
	weapon_graph_setting_entry_t settings[WEAPON_GRAPH_SETTINGS_MAX];
	s32 setting_count;
	weapon_graph_setting_entry_t variables[WEAPON_GRAPH_SETTINGS_MAX];
	s32 variable_count;
} weapon_graph_weapon_settings_t;

/* c3849 Wave 5f Unit 9: camera_effect latches to an s32 enum at parse so the
 * bgunTick vision arm never strcmps per tick (binding spec B3). */
#define WEAPON_GRAPH_CAMERA_EFFECT_NONE 0
#define WEAPON_GRAPH_CAMERA_EFFECT_XRAY 1

typedef struct weapon_graph_ir_node {
	char id[WEAPON_GRAPH_IR_ID_LEN];
	char kind[WEAPON_GRAPH_IR_ID_LEN];
	char subgraph[WEAPON_GRAPH_IR_ID_LEN];
	weapon_graph_opcode_e opcode;
	s32 param_start;
	s32 param_count;
} weapon_graph_ir_node_t;

typedef struct weapon_graph_ir_edge {
	s32 from;
	s32 to;
} weapon_graph_ir_edge_t;

typedef struct weapon_graph_ir_export {
	char name[WEAPON_GRAPH_IR_ID_LEN];
	s32 node;
} weapon_graph_ir_export_t;

typedef struct weapon_graph_ir_context {
	char name[WEAPON_GRAPH_IR_ID_LEN];
	char scope[WEAPON_GRAPH_IR_ID_LEN];
	char source[WEAPON_GRAPH_IR_ID_LEN];
	char type[WEAPON_GRAPH_IR_ID_LEN];
	char lifetime[WEAPON_GRAPH_IR_ID_LEN];
} weapon_graph_ir_context_t;

typedef struct weapon_graph_ir_subgraph {
	char id[WEAPON_GRAPH_IR_ID_LEN];
	char entry[WEAPON_GRAPH_IR_ID_LEN];
	s32 entry_node;
} weapon_graph_ir_subgraph_t;

typedef struct weapon_graph_ir {
	asset_type_e asset_type;
	char schema[WEAPON_GRAPH_IR_ID_LEN];
	char asset_id[CATALOG_ID_LEN];
	char graph_id[WEAPON_GRAPH_IR_ID_LEN];
	char source_sha256[SHA256_HEX_SIZE];
	char ir_sha256[SHA256_HEX_SIZE];

	weapon_graph_ir_context_t *contexts;
	s32 context_count;
	size_t context_capacity;
	weapon_graph_ir_node_t nodes[WEAPON_GRAPH_IR_MAX_NODES];
	s32 node_count;
	weapon_graph_ir_edge_t edges[WEAPON_GRAPH_IR_MAX_EDGES];
	s32 edge_count;
	weapon_graph_ir_export_t *exports;
	s32 export_count;
	size_t export_capacity;
	weapon_graph_ir_subgraph_t *subgraphs;
	s32 subgraph_count;
	size_t subgraph_capacity;
	weapon_graph_ir_param_t params[WEAPON_GRAPH_IR_MAX_PARAMS];
	s32 param_count;
} weapon_graph_ir_t;

typedef struct weapon_graph_held_function {
	s32 valid;
	weapon_graph_opcode_e opcode;
	char parity_module[WEAPON_GRAPH_IR_ID_LEN];
	char node_id[WEAPON_GRAPH_IR_ID_LEN];
	char mode[16];
	char function_type[32];
	s32 has_function_type_id;
	s32 function_type_id;
	s32 ammo_slot;
	u32 flags;
	char trigger_policy[32];
	s32 has_burst_count;
	s32 burst_count;

	s32 has_damage;
	f32 damage;
	s32 has_spread;
	f32 spread;
	s32 has_recoil_anim_unk24;
	s32 recoil_anim_unk24;
	s32 has_recoil_anim_unk25;
	s32 recoil_anim_unk25;
	s32 has_recoil_anim_unk26;
	s32 recoil_anim_unk26;
	s32 has_recoil_anim_unk27;
	s32 recoil_anim_unk27;
	s32 has_recoildist;
	f32 recoildist;
	s32 has_recoilangle;
	f32 recoilangle;
	s32 has_slidemax;
	f32 slidemax;
	s32 has_impactforce;
	f32 impactforce;
	s32 has_duration_ticks60;
	u8 duration_ticks60;
	s32 has_shootsound;
	u16 shootsound;
	s32 has_penetration;
	u8 penetration;
	s32 has_initial_rpm;
	f32 initial_rpm;
	s32 has_max_rpm;
	f32 max_rpm;
	s32 has_turret_accel;
	s32 turret_accel;
	s32 has_turret_decel;
	s32 turret_decel;
	s32 has_recoverytime_ticks60;
	s32 recoverytime_ticks60;
	s32 has_projectile_modelnum;
	s32 projectile_modelnum;
	char projectile_model_ref[WEAPON_GRAPH_IR_VALUE_LEN];
	char projectile_ref[CATALOG_ID_LEN];
	char entity_ref[CATALOG_ID_LEN];
	char payload_ref[CATALOG_ID_LEN];
	s32 has_scale;
	f32 scale;
	s32 has_speed;
	f32 speed;
	s32 has_travel_distance;
	s32 travel_distance;
	s32 has_timer_ticks60;
	s32 timer_ticks60;
	s32 has_reflect_angle;
	f32 reflect_angle;
	s32 has_soundnum;
	s32 soundnum;
	s32 has_activation_time_ticks60;
	s32 activation_time_ticks60;
	s32 has_recovery_time_ticks60;
	s32 recovery_time_ticks60;
	s32 has_range;
	f32 range;
	s32 has_specialfunc;
	s32 specialfunc;
	s32 has_device;
	u32 device;
	s32 has_sight;
	u32 sight;
	s32 has_zoom_fov;
	f32 zoom_fov;
	char reticle_ref[CATALOG_ID_LEN];
	char overlay_ref[CATALOG_ID_LEN];
	char camera_effect[WEAPON_GRAPH_IR_VALUE_LEN];
	/* c3849 Wave 5f: WEAPON_GRAPH_CAMERA_EFFECT_* latched at parse; unknown
	 * camera_effect values get a one-time compile LOG_NOTE and latch NONE. */
	s32 camera_effect_mode;
} weapon_graph_held_function_t;

typedef struct weapon_graph_projectile_runtime {
	s32 valid;
	char asset_id[CATALOG_ID_LEN];
	char graph_id[WEAPON_GRAPH_IR_ID_LEN];
	char parity_module[WEAPON_GRAPH_IR_ID_LEN];
	char source_sha256[SHA256_HEX_SIZE];
	char ir_sha256[SHA256_HEX_SIZE];

	char model_ref[WEAPON_GRAPH_IR_VALUE_LEN];
	char model_archive[FS_MAXPATH];
	s32 has_projectile_modelnum;
	s32 projectile_modelnum;
	char source_mode[16];
	char source_function_type[32];
	s32 has_source_function_type_id;
	s32 source_function_type_id;
	u32 flags;

	s32 has_scale;
	f32 scale;
	s32 has_damage;
	f32 damage;
	char motion_kind[64];
	s32 has_speed;
	f32 speed;
	s32 has_travel_distance;
	s32 travel_distance;
	s32 has_timer60;
	s32 timer60;
	s32 has_activation_time60;
	s32 activation_time60;
	s32 has_recovery_time60;
	s32 recovery_time60;
	s32 has_reflect_angle;
	f32 reflect_angle;
	s32 powered;
	s32 calculate_trajectory;

	s32 has_trajectory_correction;
	char trajectory_aim_source[64];
	s32 trajectory_solve_velocity;
	f32 trajectory_max_angle;

	s32 has_homing;
	char homing_target_source[64];
	char homing_target_filter[64];
	char homing_lost_target_behavior[64];
	char homing_retarget_policy[64];
	char homing_runtime_constants[64];
	f32 homing_steering_gain;
	f32 homing_steering_damping;

	s32 has_fly_by_wire;
	char fly_control_source[64];
	char fly_bot_route_policy[64];
	char fly_owner_death_behavior[64];
	f32 fly_turn_rate;
	f32 fly_acceleration;
	f32 fly_enemy_proximity_radius;
	f32 fly_max_altitude;
	s32 fly_lost_target_timeout_ticks60;
	s32 fly_smoke_interval_ticks60;

	s32 has_wall_hugger;
	char wall_stick_surface_filter[64];
	char wall_fall_vector[64];
	char wall_explosion_ref[CATALOG_ID_LEN];
	s32 wall_stick_timer_ticks60;
	f32 wall_fall_threshold;
	s32 wall_post_fall_timer60;

	s32 has_sticky_attach;
	char sticky_surface_filter[64];
	char sticky_prop_filter[64];
	char sticky_embed_policy[64];
	char sticky_on_attach[64];
	s32 sticky_allow_background;
	s32 sticky_allow_char;
	s32 sticky_allow_obj;

	s32 has_bounce_slide;
	s32 bounce_limit;
	f32 bounce_first_boost;
	f32 bounce_rest_speed;
	f32 bounce_slide_friction;
	s32 bounce_randomize_rotation;

	s32 has_timer;
	s32 timer_ticks60;
	char timer_starts[64];
	char timer_on_expire[64];

	s32 has_impact;
	char impact_filter[64];
	char impact_explosion_ref[CATALOG_ID_LEN];
	char impact_spark_ref[CATALOG_ID_LEN];
	s32 impact_hit_sound;
	s32 impact_consume_on_hit;
	s32 impact_stick_on_hit;

	s32 has_trail;
	char trail_type[64];
	s32 trail_interval_ticks60;

	s32 has_transition_to_entity;
	char entity_ref[CATALOG_ID_LEN];
	char transition_when[64];
	s32 transfer_owner;
	s32 transfer_ammo;
	s32 transfer_position;
	s32 delete_carrier;

	s32 has_pickup_recover;
	s32 pickup_timer_ticks60;
	char pickup_allowed_owner[64];
	char recover_weapon_ref[CATALOG_ID_LEN];
	char recover_ammo_policy[64];
	s32 pickup_sound;

	/* c3849 Unit 1c: registration-time derived fields. String policies latch
	 * to s32 enums at parse so weaponTick/projectileTick consumers never
	 * strcmp per event (binding spec B3). Defaults are pre-set in
	 * weaponGraphRuntimeRegisterProjectileIrOwned before the node loop. */
	s32 timer_start_policy;   /* 0 on_spawn (default), 1 on_impact, 2 on_attach */
	s32 timer_expire_policy;  /* 0 explode (default), 1 delete */
	f32 wall_fall_vec[3];     /* default {0,-10,0}; accepts "down" or "x,y,z" */
	s32 wall_stick_bg_only;   /* wall_stick_surface_filter == "background" */
	s32 impact_filter_mode;   /* 0 any/"", 1 background, 2 props, 3 chr */
	s32 impact_exptype;       /* EXPLOSIONTYPE_* from impact_explosion_ref; -1 unresolved */
	s32 trail_smoketype;      /* SMOKETYPE_* from trail_type; -1 none */
	s32 recover_weaponnum;    /* WEAPON_* from recover_weapon_ref; -1 unresolved */
	s32 pickup_owner_only;    /* pickup_allowed_owner == "owner"/"owner_only" */
	s32 recover_ammo_none;    /* recover_ammo_policy == "none" */
} weapon_graph_projectile_runtime_t;

typedef struct weapon_graph_entity_runtime {
	s32 valid;
	char asset_id[CATALOG_ID_LEN];
	char graph_id[WEAPON_GRAPH_IR_ID_LEN];
	char parity_module[WEAPON_GRAPH_IR_ID_LEN];
	char source_sha256[SHA256_HEX_SIZE];
	char ir_sha256[SHA256_HEX_SIZE];

	char archetype[64];
	char model_ref[WEAPON_GRAPH_IR_VALUE_LEN];
	char model_archive[FS_MAXPATH];
	s32 has_projectile_modelnum;
	s32 projectile_modelnum;
	char source_mode[16];
	u32 flags;
	s32 has_activation_time60;
	s32 activation_time60;
	s32 has_recovery_time60;
	s32 recovery_time60;
	char runtime_detail[WEAPON_GRAPH_IR_VALUE_LEN];

	s32 has_armed_explosive;
	s32 arm_delay_ticks60;
	char detonation_policy[64];
	char explosion_ref[CATALOG_ID_LEN];
	char armed_owner_filter[64];
	char damage_response[64];
	s32 delete_on_detonate;

	s32 has_proxy_trigger;
	f32 proxy_radius;
	char proxy_target_filter[64];
	char proxy_team_filter[64];
	char proxy_owner_filter[64];
	s32 proxy_line_of_sight;
	char proxy_on_trigger[64];

	s32 has_remote_detonatable;
	char detonator_ref[CATALOG_ID_LEN];
	char owner_slot_source[64];
	char coop_policy[64];
	char anti_policy[64];
	char self_attached_policy[64];
	char on_remote_signal[64];

	s32 has_timed_detonatable;
	s32 timed_timer_ticks60;
	char timed_starts_when[64];
	char timed_on_expire[64];
	char timed_pause_policy[64];

	s32 has_nbomb_storm;
	char storm_ref[CATALOG_ID_LEN];
	char storm_owner_transfer[64];
	char storm_activation_policy[64];
	s32 storm_delete_carrier;

	s32 has_autogun;
	char autogun_target_filter[64];
	char autogun_team_policy[64];
	char autogun_net_authority[64];
	s32 autogun_friendly_fire_suppression;
	s32 autogun_pickup_recover;
	f32 autogun_aim_distance;
	f32 autogun_turn_speed;
	f32 autogun_fire_cadence;
	s32 autogun_alternate_muzzles;
	s32 autogun_beam_interval_ticks60;
	s32 autogun_ammo_reserve;

	s32 has_sticky_device;
	char sticky_attachment_filter[64];
	char mission_behavior_ref[CATALOG_ID_LEN];
	char sticky_pickup_policy[64];
	char sticky_disable_policy[64];
	char sticky_visible_state[64];

	s32 has_owner_cleanup;
	char owner_lost_behavior[64];
	char owner_death_behavior[64];
	char replace_existing_policy[64];
	s32 max_active_per_owner;

	s32 has_interaction;
	char interact_filter[64];
	char interaction_action[64];
	char prompt_ref[CATALOG_ID_LEN];
	s32 interaction_sound;
	char transfer_payload[CATALOG_ID_LEN];

	/* c3849 Unit 1c: registration-time derived fields (binding spec B3).
	 * Mode defaults (0 = OG behavior) and the -1 sentinels are pre-set in
	 * weaponGraphRuntimeRegisterEntityIrOwned before the node loop, so an
	 * absent bool param stays -1 (OG default) while an authored 0/1 is
	 * preserved (absent-vs-authored-0 disambiguation). */
	s32 armed_damage_response_mode; /* 0 detonate (OG), 1 ignore */
	s32 armed_exptype;              /* EXPLOSIONTYPE_* from explosion_ref; -1 unresolved */
	s32 proxy_on_trigger_mode;      /* 0 detonate (OG), 1 storm */
	s32 proxy_target_filter_mode;   /* 0 all (OG), 1 hostile_chr */
	s32 proxy_team_filter_mode;     /* 0 all (OG), 1 enemy_only */
	s32 proxy_owner_filter_mode;    /* 0 all (OG), 1 exclude_owner, 2 owner_only */
	s32 remote_signal_mode;         /* 0 detonate (OG), 1 storm */
	s32 timed_on_expire_mode;       /* 0 detonate (OG), 1 storm, 2 delete */
	s32 timed_starts_mode;          /* 0 thrown/armed (OG default) */

	/* c3849 Wave 5 Unit 7 (entity-deployed): sticky-device policy strings
	 * latch to s32 at parse so the stick gate / pickup gate / landing arm in
	 * propobj.c never strcmp (binding spec B3). max_active_per_owner is
	 * sentinel-preset to -1 in the registration path: absent = OG (one slot
	 * per player); authored 0 = deployment denied. */
	s32 sticky_attachment_bg_only;  /* sticky_attachment_filter == "background_only" */
	s32 sticky_pickup_none;         /* sticky_pickup_policy == "none" */
	s32 sticky_disable_shootable;   /* sticky_disable_policy == "shootable" */
	s32 sticky_visible_hidden;      /* sticky_visible_state == "hidden" */
} weapon_graph_entity_runtime_t;

const char *weaponGraphSchemaForType(asset_type_e type);
const char *weaponGraphOpcodeName(weapon_graph_opcode_e opcode);
weapon_graph_opcode_e weaponGraphOpcodeForKind(asset_type_e graph_type,
                                               const char *kind);
size_t weaponGraphParityModuleCount(void);
const weapon_graph_parity_module_t *weaponGraphParityModuleAt(size_t index);
const weapon_graph_parity_module_t *weaponGraphParityModuleForOpcode(
	weapon_graph_opcode_e opcode);
const char *weaponGraphParityModuleNameForOpcode(weapon_graph_opcode_e opcode);

/* ABI-compatible parse latch. Selected public effect IDs are not native
 * literals, so resolution stays deferred to the active .pdeffect executor. */
s32 weaponGraphResolveExplosionRef(const char *ref);

/* c3849 Unit 1c: pure rpm -> autogunTickShoot fire-interval conversion.
 * 1800 rpm is the OG laptop baseline (interval 1); 900 rpm -> 2; rpm <= 0
 * (absent) and out-of-range values clamp to 1 (OG cadence). */
s32 weaponGraphAutogunFireInterval(f32 rpm);

/* c3849 Wave 5 Unit 6: detonator provenance predicate for the custom remote
 * sub-branch. Empty detonator_ref accepts any signal weaponnum including the
 * -1 wildcard (OG parity); an authored ref demands an exact runtime
 * weaponnum match (-1 never satisfies it); an unresolved ref never matches
 * (one-time LOG_WARNING). Lazy catalog resolve at signal time only. */
s32 weaponGraphEntityRemoteSignalMatches(
	const weapon_graph_entity_runtime_t *entity, s32 weaponnum);

s32 weaponGraphRuntimeEnabled(void);
/* Test hook: product builds ignore attempts to disable the runtime after the
 * Wave 7 cutover. */
void weaponGraphRuntimeSetEnabled(s32 enabled);

void weaponGraphRuntimeClearWeapon(s32 weaponnum);
void weaponGraphRuntimeClearAsset(const char *asset_id);
void weaponGraphRuntimeClearAll(void);
s32 weaponGraphRuntimeRegisterHeldIr(s32 weaponnum, const weapon_graph_ir_t *ir,
                                     char *err, size_t err_cap);
s32 weaponGraphRuntimeRegisterWeaponArchive(s32 weaponnum,
                                            const char *archive_path,
                                            char *err, size_t err_cap);
s32 weaponGraphRuntimeRegisterWeaponGraphJson(s32 weaponnum,
                                              const char *asset_id,
                                              const char *json,
                                              u32 json_size,
                                              char *err,
                                              size_t err_cap);
/* c3849 Wave 5f Unit 2: extended with the settings/variables/presentation
 * authoring sources so the loose path matches the archive path. variables
 * feed $name substitution at compile (B6.3); settings + presentation
 * sight/zoom_fov feed the per-weapon defaults layer. Either may be
 * NULL/empty (no tunables, no substitution scope). */
s32 weaponGraphRuntimeRegisterWeaponSourceJson(s32 weaponnum,
                                               const char *asset_id,
                                               const char *primary_json,
                                               u32 primary_size,
                                               const char *secondary_json,
                                               u32 secondary_size,
                                               const char *shared_json,
                                               u32 shared_size,
                                               const char *settings_json,
                                               u32 settings_size,
                                               const char *variables_json,
                                               u32 variables_size,
                                               const char *presentation_json,
                                               u32 presentation_size,
                                               char *err,
                                               size_t err_cap);
s32 weaponGraphRuntimeRegisterProjectileIr(const weapon_graph_ir_t *ir,
                                           char *err, size_t err_cap);
s32 weaponGraphRuntimeRegisterEntityIr(const weapon_graph_ir_t *ir,
                                       char *err, size_t err_cap);
s32 weaponGraphRuntimeRegisterBehaviorGraphJson(asset_type_e graph_type,
                                                const char *asset_id,
                                                const char *json,
                                                u32 json_size,
                                                char *err,
                                                size_t err_cap);
s32 weaponGraphRuntimeRegisterBehaviorArchive(asset_type_e graph_type,
                                              const char *archive_path,
                                              char *err, size_t err_cap);
const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunction(
	s32 weaponnum, s32 funcindex);
const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunctionForGameplay(
	s32 weaponnum, s32 funcindex);
/* c3849 Wave 5f Unit 2: per-weapon tunables accessors. The ungated form is
 * for registration/editor surfaces; gameplay consumers go through the
 * toggle-gated ForGameplay form like every other accessor pair. */
const weapon_graph_weapon_settings_t *weaponGraphRuntimeGetWeaponSettings(
	s32 weaponnum);
const weapon_graph_weapon_settings_t *weaponGraphRuntimeGetWeaponSettingsForGameplay(
	s32 weaponnum);
const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectile(
	const char *asset_id);
const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectileForGameplay(
	const char *asset_id);
const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectileForHeldFunction(
	const weapon_graph_held_function_t *held);
const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntity(
	const char *asset_id);
const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForGameplay(
	const char *asset_id);
const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForHeldFunction(
	const weapon_graph_held_function_t *held);
const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForProjectile(
	const weapon_graph_projectile_runtime_t *projectile);

s32 weaponGraphValidateJson(asset_type_e graph_type, const char *json,
                            u32 json_size, char *err, size_t err_cap);
s32 weaponGraphCompileJson(asset_type_e graph_type, const char *json,
                           u32 json_size, weapon_graph_ir_t *out,
                           char *err, size_t err_cap);
void weaponGraphIrFree(weapon_graph_ir_t *ir);
s32 weaponGraphCompileArchiveFile(const char *archive_path,
                                  asset_type_e graph_type,
                                  weapon_graph_ir_t *out,
                                  char *err, size_t err_cap);
s32 weaponGraphCompileWeaponSourceJson(const char *asset_id,
                                       const char *primary_json,
                                       u32 primary_size,
                                       const char *secondary_json,
                                       u32 secondary_size,
                                       const char *shared_json,
                                       u32 shared_size,
                                       weapon_graph_ir_t *out,
                                       char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_WEAPON_GRAPH_RUNTIME_H */
