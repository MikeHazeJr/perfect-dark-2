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
#define WEAPON_GRAPH_IR_MAX_EXPORTS 16
#define WEAPON_GRAPH_IR_MAX_PARAMS  512
#define WEAPON_GRAPH_IR_MAX_CONTEXTS 16
#define WEAPON_GRAPH_IR_MAX_SUBGRAPHS 8
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
} weapon_graph_opcode_e;

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

	weapon_graph_ir_context_t contexts[WEAPON_GRAPH_IR_MAX_CONTEXTS];
	s32 context_count;
	weapon_graph_ir_node_t nodes[WEAPON_GRAPH_IR_MAX_NODES];
	s32 node_count;
	weapon_graph_ir_edge_t edges[WEAPON_GRAPH_IR_MAX_EDGES];
	s32 edge_count;
	weapon_graph_ir_export_t exports[WEAPON_GRAPH_IR_MAX_EXPORTS];
	s32 export_count;
	weapon_graph_ir_subgraph_t subgraphs[WEAPON_GRAPH_IR_MAX_SUBGRAPHS];
	s32 subgraph_count;
	weapon_graph_ir_param_t params[WEAPON_GRAPH_IR_MAX_PARAMS];
	s32 param_count;
} weapon_graph_ir_t;

typedef struct weapon_graph_held_function {
	s32 valid;
	weapon_graph_opcode_e opcode;
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
} weapon_graph_held_function_t;

const char *weaponGraphSchemaForType(asset_type_e type);
const char *weaponGraphOpcodeName(weapon_graph_opcode_e opcode);
weapon_graph_opcode_e weaponGraphOpcodeForKind(asset_type_e graph_type,
                                               const char *kind);

s32 weaponGraphRuntimeEnabled(void);
void weaponGraphRuntimeSetEnabled(s32 enabled);
void weaponGraphRuntimeClearWeapon(s32 weaponnum);
void weaponGraphRuntimeClearAll(void);
s32 weaponGraphRuntimeRegisterHeldIr(s32 weaponnum, const weapon_graph_ir_t *ir,
                                     char *err, size_t err_cap);
s32 weaponGraphRuntimeRegisterWeaponArchive(s32 weaponnum,
                                            const char *archive_path,
                                            char *err, size_t err_cap);
const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunction(
	s32 weaponnum, s32 funcindex);
const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunctionForGameplay(
	s32 weaponnum, s32 funcindex);

s32 weaponGraphValidateJson(asset_type_e graph_type, const char *json,
                            u32 json_size, char *err, size_t err_cap);
s32 weaponGraphCompileJson(asset_type_e graph_type, const char *json,
                           u32 json_size, weapon_graph_ir_t *out,
                           char *err, size_t err_cap);
s32 weaponGraphCompileArchiveFile(const char *archive_path,
                                  asset_type_e graph_type,
                                  weapon_graph_ir_t *out,
                                  char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_WEAPON_GRAPH_RUNTIME_H */
