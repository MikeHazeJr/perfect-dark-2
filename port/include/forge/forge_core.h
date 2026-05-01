/**
 * forge_core.h -- The Grid (Forge) level editor data model.
 *
 * Single-header data model covering F1 (catalog + placement) through F8
 * (stretch goals). Downstream UI renderers (pdgui_forge_*.cpp) and the
 * runtime modules (forge_undo.c, forge_serialize.c, etc.) include this one
 * header to get the full universe of types.
 *
 * Storage: a small fixed-capacity pool per object category. This avoids
 * dynamic allocation in the editor hot path while keeping budget enforcement
 * trivial. Sizes match the soft/hard limits documented in the design doc
 * §3.3.
 *
 * All API is C-callable. C++ renderers include this via an extern "C" wrap.
 */

#ifndef _IN_FORGE_CORE_H
#define _IN_FORGE_CORE_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Capacity limits (tunable; match design doc budget)
 * ============================================================ */

#define FORGE_MAX_OBJECTS       1024
#define FORGE_MAX_ZONES         128
#define FORGE_MAX_LIGHTS        64
#define FORGE_MAX_LOGIC_NODES   256
#define FORGE_MAX_LOGIC_WIRES   512
#define FORGE_MAX_CHANNELS      32
#define FORGE_MAX_WAVES         32
#define FORGE_MAX_OBJECTIVES    16
#define FORGE_MAX_SELECTED      64
#define FORGE_MAX_PREFABS       64
#define FORGE_MAX_UNDO          256
#define FORGE_MAX_CATALOG       512
#define FORGE_MAX_DEPENDENCIES  128    /* unique catalog IDs referenced by map */

#define FORGE_ID_LEN            48
#define FORGE_LABEL_LEN         32
#define FORGE_NAME_LEN          64
#define FORGE_DESC_LEN          256
#define FORGE_TEXT_LEN          128
#define FORGE_PATH_LEN          192

/* ============================================================
 * Categories (top-level catalog taxonomy — design §3.1)
 * ============================================================ */

typedef enum forge_category {
	FORGE_CAT_GEOMETRY = 0,
	FORGE_CAT_PROP,
	FORGE_CAT_WEAPON_PAD,
	FORGE_CAT_SPAWN_POINT,
	FORGE_CAT_PICKUP,
	FORGE_CAT_LIGHT,
	FORGE_CAT_EFFECT,
	FORGE_CAT_ZONE,
	FORGE_CAT_INTERACTABLE,
	FORGE_CAT_AI,
	FORGE_CAT_LOGIC,
	FORGE_CAT_PREFAB,
	FORGE_CAT_COUNT
} forge_category_t;

/* Collision mode (design §5.1). */
typedef enum forge_collision_mode {
	FORGE_COLLISION_SOLID = 0,
	FORGE_COLLISION_PASSTHROUGH = 1,
	FORGE_COLLISION_PROJECTILE_ONLY = 2
} forge_collision_mode_t;

/* Zone types (design §6.1). */
typedef enum forge_zone_type {
	FORGE_ZONE_TRIGGER = 0,
	FORGE_ZONE_RADIATION,
	FORGE_ZONE_GRAVITY,
	FORGE_ZONE_TELEPORTER,
	FORGE_ZONE_KILL,
	FORGE_ZONE_WATER,
	FORGE_ZONE_SOUND,
	FORGE_ZONE_NO_WEAPON,
	FORGE_ZONE_FOG,
	FORGE_ZONE_SOFT_BOUND,
	FORGE_ZONE_SPAWN_AREA,     /* team spawn zone */
	FORGE_ZONE_HILL,           /* KotH */
	FORGE_ZONE_TERRITORY,      /* territories */
	FORGE_ZONE_COUNT
} forge_zone_type_t;

typedef enum forge_zone_shape {
	FORGE_ZONE_SHAPE_BOX = 0,
	FORGE_ZONE_SHAPE_SPHERE = 1
} forge_zone_shape_t;

/* Light types (design §8.3). */
typedef enum forge_light_type {
	FORGE_LIGHT_POINT = 0,
	FORGE_LIGHT_SPOT,
	FORGE_LIGHT_AREA,
	FORGE_LIGHT_EMISSIVE,
	FORGE_LIGHT_TYPE_COUNT
} forge_light_type_t;

/* Logic system (design §7). */
typedef enum forge_logic_kind {
	FORGE_LOGIC_EVENT = 0,
	FORGE_LOGIC_CONDITION = 1,
	FORGE_LOGIC_ACTION = 2
} forge_logic_kind_t;

/* Enum of all logic node sub-types. String "kind" mirrors this for save. */
typedef enum forge_logic_op {
	/* events */
	FORGE_OP_ON_PLAYER_ENTER = 0,
	FORGE_OP_ON_PLAYER_EXIT,
	FORGE_OP_ON_OBJECT_DESTROYED,
	FORGE_OP_ON_SWITCH,
	FORGE_OP_ON_KILL,
	FORGE_OP_ON_KILL_COUNT,
	FORGE_OP_ON_ITEM_PICKUP,
	FORGE_OP_ON_TIMER,
	FORGE_OP_ON_ROUND_START,
	FORGE_OP_ON_ROUND_END,
	FORGE_OP_ON_CHANNEL,
	FORGE_OP_ON_INTERACT,
	/* conditions */
	FORGE_OP_HAS_ITEM,
	FORGE_OP_KILL_COUNT_GE,
	FORGE_OP_ALL_ENEMIES_DEAD,
	FORGE_OP_SWITCH_STATE,
	FORGE_OP_CHANNEL_STATE,
	FORGE_OP_TEAM_SCORE_GE,
	FORGE_OP_PLAYER_COUNT,
	FORGE_OP_TIMER_ELAPSED,
	FORGE_OP_RANDOM,
	/* actions */
	FORGE_OP_OPEN_DOOR,
	FORGE_OP_CLOSE_DOOR,
	FORGE_OP_ACTIVATE_ELEVATOR,
	FORGE_OP_SPAWN_OBJECT,
	FORGE_OP_SPAWN_AI,
	FORGE_OP_DESTROY_OBJECT,
	FORGE_OP_PLAY_SOUND,
	FORGE_OP_SHOW_MESSAGE,
	FORGE_OP_SET_CHANNEL,
	FORGE_OP_TELEPORT_PLAYER,
	FORGE_OP_CHANGE_ZONE,
	FORGE_OP_SET_TIMER,
	FORGE_OP_AWARD_SCORE,
	FORGE_OP_END_MISSION,
	FORGE_OP_LOCK_DOOR,
	FORGE_OP_UNLOCK_DOOR,
	FORGE_OP_ENABLE_OBJECT,
	FORGE_OP_DISABLE_OBJECT,
	FORGE_OP_CAMERA_EVENT,
	FORGE_OP_SPAWN_WAVE,        /* F5b */
	FORGE_OP_TRIGGER_BOSS_PHASE, /* F5b */
	FORGE_OP_OBJECTIVE_COMPLETE, /* F7 */
	FORGE_OP_OBJECTIVE_FAIL,    /* F7 */
	FORGE_OP_COUNT
} forge_logic_op_t;

/* Game type structure (design §11.5 / §13). */
typedef enum forge_gametype_structure {
	FORGE_GT_SINGLE = 0,
	FORGE_GT_BEST_OF_N,
	FORGE_GT_WAVE_BASED,
	FORGE_GT_PHASE_BASED,
	FORGE_GT_STRUCTURE_COUNT
} forge_gametype_structure_t;

typedef enum forge_gametype_win_cond {
	FORGE_WIN_SCORE_TARGET = 0,
	FORGE_WIN_LAST_ALIVE,
	FORGE_WIN_TIMER_EXPIRES,
	FORGE_WIN_BOSS_KILLED,
	FORGE_WIN_OBJECTIVES_COMPLETE,
	FORGE_WIN_COUNT
} forge_gametype_win_cond_t;

typedef enum forge_gametype_role {
	FORGE_ROLE_NONE = 0,
	FORGE_ROLE_INFECTED,
	FORGE_ROLE_VIP,
	FORGE_ROLE_JUGGERNAUT,
	FORGE_ROLE_DEFENDER,
	FORGE_ROLE_HUNTER,
	FORGE_ROLE_COUNT
} forge_gametype_role_t;

/* Modifier flag bits for game type. */
#define FORGE_MOD_LOW_GRAVITY     0x0001
#define FORGE_MOD_ONE_HIT_KILLS   0x0002
#define FORGE_MOD_INFINITE_AMMO   0x0004
#define FORGE_MOD_NO_RADAR        0x0008
#define FORGE_MOD_FRIENDLY_FIRE   0x0010
#define FORGE_MOD_NO_AUTO_AIM     0x0020
#define FORGE_MOD_FAST_MOVE       0x0040
#define FORGE_MOD_TEAM_SHUFFLE    0x0080

/* Objectives (F7). */
typedef enum forge_objective_kind {
	FORGE_OBJ_PRIMARY = 0,
	FORGE_OBJ_SECONDARY,
	FORGE_OBJ_BONUS
} forge_objective_kind_t;

typedef enum forge_objective_status {
	FORGE_OBJ_PENDING = 0,
	FORGE_OBJ_COMPLETE,
	FORGE_OBJ_FAILED
} forge_objective_status_t;

/* AI behaviors (design §5.6). */
typedef enum forge_ai_behavior {
	FORGE_AI_PATROL = 0,
	FORGE_AI_GUARD,
	FORGE_AI_AGGRESSIVE,
	FORGE_AI_PASSIVE,
	FORGE_AI_SCRIPTED,
	FORGE_AI_BEHAVIOR_COUNT
} forge_ai_behavior_t;

/* Door open direction (design §5.5). */
typedef enum forge_door_dir {
	FORGE_DOOR_SLIDE_LEFT = 0,
	FORGE_DOOR_SLIDE_RIGHT,
	FORGE_DOOR_SLIDE_UP,
	FORGE_DOOR_SWING
} forge_door_dir_t;

/* Switch activation (design §5.5). */
typedef enum forge_switch_activation {
	FORGE_SWITCH_INTERACT = 0,
	FORGE_SWITCH_SHOOT,
	FORGE_SWITCH_PROXIMITY
} forge_switch_activation_t;

/* Spawn point type (design §5.4). */
typedef enum forge_spawn_type {
	FORGE_SPAWN_INITIAL = 0,
	FORGE_SPAWN_RESPAWN,
	FORGE_SPAWN_BOTH
} forge_spawn_type_t;

/* S310 R2/R4: Weapon source.  Maps declare weapon pads with a default
 * weapon catalog ID per pad.  At match start this setting decides whose
 * choice wins: the map author's per-pad default, the match lobby
 * weapon set, or a hybrid where map defaults fill in any "Any Weapon"
 * pads.  Modded weapons participate transparently -- the dependency
 * track in map.json ensures the client has them available (see R3).
 *
 * The match setup screen also exposes a "Use Map Defaults" checkbox --
 * when checked, `weapon_source` is forced to MAP_DEFAULTS for the
 * duration of that match regardless of the lobby's normal weapon-set
 * setting. */
typedef enum forge_weapon_source {
	FORGE_WEAPONS_MAP_DEFAULTS    = 0, /* each pad spawns its map-author-chosen default */
	FORGE_WEAPONS_MATCH_OVERRIDE  = 1, /* lobby weapon set overrides every pad */
	FORGE_WEAPONS_PREFER_MAP      = 2, /* pad defaults win for pads with a specific weapon; "Any" pads use lobby choice */
} forge_weapon_source_t;

/* S313: Map variant mode.  A map can be either built from scratch
 * (NEW_EMPTY -- default -- objects placed from zero) or authored as a
 * delta on top of an existing stage (EDIT_EXISTING -- the base stage's
 * props are imported, marked `from_base`, and only author changes are
 * stored in the save as deltas).  Saves of variant maps carry a
 * `variant_source_slug` (another Grid map) or a `base_stage_id`
 * (engine stage) pointer so the loader can re-import the base on
 * open. */
typedef enum forge_map_variant_mode {
	FORGE_VARIANT_NEW_EMPTY   = 0, /* blank canvas, objects placed from scratch */
	FORGE_VARIANT_EDIT_STAGE  = 1, /* start from a base engine stage's props */
	FORGE_VARIANT_EDIT_MAP    = 2, /* start from another Grid map's objects */
} forge_map_variant_mode_t;

/* S313: Bot spawn mode.  Author-side declaration of how bots should be
 * placed at runtime during live testing.  Gameplay code is not yet
 * wired to act on these -- they are a forge-side data capture so the
 * editor can publish intent today, and the runtime hook lands in a
 * follow-up polish pass alongside engine botmgr integration. */
typedef enum forge_bot_spawn_mode {
	FORGE_BOT_SPAWN_ANY       = 0, /* default -- respawn at any spawn point */
	FORGE_BOT_SPAWN_NEAR_ME   = 1, /* spawn within radius of requesting player */
	FORGE_BOT_SPAWN_SMART     = 2, /* aggressively seek combat for flow testing */
} forge_bot_spawn_mode_t;

/* ============================================================
 * Catalog entry (what can be placed)
 * ============================================================ */

typedef struct forge_catalog_entry {
	char id[FORGE_ID_LEN];             /* catalog ID string */
	char name[FORGE_NAME_LEN];         /* display name */
	forge_category_t category;
	const char *subcategory;            /* string ptr into static table */
	const char *tags;                   /* comma-separated tags */
	u16 tri_cost;                       /* triangle approximation for budget */
	u8 flags;                           /* reserved */
	u8 default_sub_type;                /* e.g. zone_type / light_type for pre-configured entries */
} forge_catalog_entry_t;

/* ============================================================
 * Placed object (in the map) — F1
 * ============================================================ */

typedef struct forge_weapon_pad_props {
	char weapon_id[FORGE_ID_LEN];       /* catalog weapon ref */
	s32 ammo;                            /* -1 = weapon default */
	u8 dual_wield;
	u8 team_lock;                        /* 0=any, 1..8 = team */
	u8 respawn_effect;                   /* 0=none,1=glow,2=hologram */
	u8 pad;
	f32 respawn_sec;                     /* 0 = one-time */
} forge_weapon_pad_props_t;

typedef struct forge_spawn_props {
	u8 type;                             /* forge_spawn_type_t */
	u8 team;                             /* 0=any, 1..8 */
	s32 priority;
	f32 facing_deg;                      /* initial look direction */
	f32 radius;                          /* for zone spawn */
} forge_spawn_props_t;

typedef struct forge_ai_props {
	char body_id[FORGE_ID_LEN];
	char head_id[FORGE_ID_LEN];
	char weapon_id[FORGE_ID_LEN];
	char dialog_alert[FORGE_NAME_LEN];
	char dialog_combat[FORGE_NAME_LEN];
	char dialog_death[FORGE_NAME_LEN];
	u8 behavior;                         /* forge_ai_behavior_t */
	u8 faction;                          /* 0=friendly,1=hostile,2=neutral */
	u8 respawn;
	u8 is_boss;                          /* F5b */
	f32 health_mult;
	f32 alert_radius;
	f32 respawn_delay_sec;
	u32 patrol_path_uid;                 /* uid of patrol point chain head */
	/* boss-specific (only valid when is_boss) */
	char boss_name[FORGE_NAME_LEN];
	f32 boss_scale;                      /* model scale 1x..5x */
	u8 num_phase_thresholds;
	u8 pad_boss[3];
	f32 phase_thresholds[4];             /* health % thresholds, sorted desc */
	u32 phase_logic_node_uids[4];        /* logic nodes to fire per phase */
} forge_ai_props_t;

typedef struct forge_door_props {
	u8 open_dir;                         /* forge_door_dir_t */
	u8 auto_close;
	u8 locked;
	u8 pad;
	f32 open_speed_sec;
	f32 auto_close_delay_sec;
	char key_id[FORGE_ID_LEN];
	char open_sound[FORGE_NAME_LEN];
	char close_sound[FORGE_NAME_LEN];
} forge_door_props_t;

typedef struct forge_elevator_props {
	u8 num_stops;
	u8 call_button;
	u8 loop;
	u8 pad;
	f32 speed_units_per_sec;
	f32 wait_time_sec;
	f32 stops_y[8];                      /* up to 8 stop Y positions */
} forge_elevator_props_t;

typedef struct forge_switch_props {
	u8 type;                             /* 0=toggle,1=momentary,2=hold */
	u8 activation;                       /* forge_switch_activation_t */
	u8 team_lock;
	u8 pad;
	f32 cooldown_sec;
	u32 target_uid;                      /* linked obj uid (optional convenience) */
	char channel_out[FORGE_NAME_LEN];    /* logic channel to set on activation */
} forge_switch_props_t;

typedef struct forge_light_props {
	u8 type;                             /* forge_light_type_t */
	u8 cast_shadows;
	u8 night_only;                       /* ToD stretch */
	u8 pad;
	f32 color[3];
	f32 intensity;
	f32 range;
	f32 inner_cone_deg;                  /* spot */
	f32 outer_cone_deg;                  /* spot */
	f32 falloff;
	f32 width;                           /* area */
	f32 height;                          /* area */
	char cookie_texture[FORGE_NAME_LEN]; /* spot cookie */
} forge_light_props_t;

typedef struct forge_zone_props {
	u8 type;                             /* forge_zone_type_t */
	u8 shape;                            /* forge_zone_shape_t */
	u8 team_filter;
	u8 once_or_repeat;                   /* 0=once,1=repeating */
	f32 size[3];                         /* box half-extents, or sphere radius in [0] */
	f32 trigger_delay_sec;
	/* damage / effect params (type-dispatched) */
	f32 damage_per_sec;
	f32 gravity_mult;
	f32 gravity_dir[3];
	f32 current_dir[3];
	f32 current_speed;
	f32 fog_color[3];
	f32 fog_density;
	u32 teleport_target_uid;             /* teleporter exit pad */
	char sound_loop_id[FORGE_NAME_LEN];  /* sound zone */
	char enter_sound_id[FORGE_NAME_LEN];
	char channel_on_enter[FORGE_NAME_LEN]; /* logic integration */
	char channel_on_exit[FORGE_NAME_LEN];
	char death_message[FORGE_NAME_LEN];  /* kill zone override */
} forge_zone_props_t;

typedef struct forge_effect_props {
	u8 kind;                             /* 0=particle,1=sound,2=decal,3=shake,4=post */
	u8 looping;
	u8 pad0;
	u8 pad1;
	f32 intensity;
	f32 range;
	char asset_id[FORGE_ID_LEN];         /* particle / sound / decal catalog ref */
	f32 color[3];
} forge_effect_props_t;

typedef struct forge_pickup_props {
	char item_id[FORGE_ID_LEN];          /* equipment / ammo catalog ref */
	s32 quantity;
	f32 respawn_sec;
	u8 team_lock;
	u8 pad[3];
} forge_pickup_props_t;

/* ============================================================
 * Placed object — universal struct
 * ============================================================ */

typedef struct forge_object {
	u32 uid;                             /* unique instance ID */
	u8 in_use;
	u8 selected;
	u8 visible;
	u8 collision_mode;                   /* forge_collision_mode_t */
	u8 category;                         /* forge_category_t */
	u8 team;                             /* 0=neutral, 1..8 */
	u8 enabled;                          /* logic disable */
	/* S313: map variant -- tracks whether this object came from the
	 * base stage/map (0 = author-placed, 1 = imported from base, 2 =
	 * imported from base then author-modified).  Saves only write
	 * in_use=1 objects, and the load path re-imports base objects
	 * alongside the author deltas so the map always reflects the
	 * latest base-stage content plus the author's changes. */
	u8 from_base;
	char catalog_id[FORGE_ID_LEN];
	char label[FORGE_LABEL_LEN];
	f32 pos[3];
	f32 rot[3];                          /* euler deg, XYZ */
	f32 scale[3];
	char material_id[FORGE_ID_LEN];      /* optional texture override */
	f32 tint[3];                         /* multiplied over base */
	f32 emissive;
	u8 cast_shadows;
	u8 lod_bias;                         /* 0=auto,1=high,2=med,3=low */
	u8 pad1[2];
	/* Type-specific props union */
	union {
		forge_weapon_pad_props_t  weapon;
		forge_spawn_props_t       spawn;
		forge_ai_props_t          ai;
		forge_door_props_t        door;
		forge_elevator_props_t    elev;
		forge_switch_props_t      sw;
		forge_light_props_t       light;
		forge_zone_props_t        zone;
		forge_effect_props_t      effect;
		forge_pickup_props_t      pickup;
	} props;
} forge_object_t;

/* ============================================================
 * Logic graph (F5)
 * ============================================================ */

typedef struct forge_logic_node {
	u32 uid;
	u8 in_use;
	u8 kind;                             /* forge_logic_kind_t */
	u8 op;                               /* forge_logic_op_t */
	u8 executed_this_frame;              /* runtime flag */
	f32 canvas_x;
	f32 canvas_y;
	char label[FORGE_LABEL_LEN];
	/* Primary params (op-specific interpretation) */
	u32 target_uid_a;
	u32 target_uid_b;
	s32 param_int_a;
	s32 param_int_b;
	f32 param_float_a;
	f32 param_float_b;
	char param_text_a[FORGE_TEXT_LEN];   /* channel / message / kind-name */
	char param_text_b[FORGE_TEXT_LEN];
} forge_logic_node_t;

typedef struct forge_logic_wire {
	u32 src_node_uid;
	u32 dst_node_uid;
	u8 src_port;
	u8 dst_port;
	u8 in_use;
	u8 pad;
} forge_logic_wire_t;

typedef struct forge_channel {
	char name[FORGE_NAME_LEN];
	u8 state;                            /* 0/1 */
	u8 in_use;
	u8 pad[2];
} forge_channel_t;

/* ============================================================
 * Wave (F5b)
 * ============================================================ */

typedef struct forge_wave {
	u8 in_use;
	u8 is_boss;
	u8 escalate;
	u8 pad;
	s32 enemy_count;
	f32 enemy_scale;
	f32 enemy_health_mult;
	f32 enemy_speed_mult;
	f32 spawn_delay_sec;
	f32 intermission_sec;
	char enemy_catalog_id[FORGE_ID_LEN];
	u32 spawn_zone_uid;                  /* zone that enemies materialize in */
} forge_wave_t;

/* ============================================================
 * Game type (F5b)
 * ============================================================ */

typedef struct forge_gametype {
	char name[FORGE_NAME_LEN];
	char description[FORGE_DESC_LEN];
	u8 structure;                        /* forge_gametype_structure_t */
	u8 win_condition;                    /* forge_gametype_win_cond_t */
	u8 num_rounds;
	u8 role_mode;                        /* forge_gametype_role_t */
	s32 score_limit;
	s32 time_limit_sec;
	u32 modifier_flags;                  /* FORGE_MOD_* bitmask */
	/* scoring rules */
	s32 score_per_kill;
	s32 score_per_headshot;
	s32 score_per_objective;
	s32 score_per_survive_sec;
	/* player setup */
	char starting_weapon[FORGE_ID_LEN];
	f32 health_mult;
	/* waves */
	s32 num_waves;
	forge_wave_t waves[FORGE_MAX_WAVES];
	/* HUD */
	u8 show_wave_counter;
	u8 show_boss_bar;
	u8 show_role_indicator;
	u8 show_survival_timer;
} forge_gametype_t;

/* ============================================================
 * Boss runtime (F5b)
 * ============================================================ */

typedef struct forge_boss_state {
	u8 active;
	u8 current_phase;
	u8 num_phases;
	u8 pad;
	u32 chr_uid;
	f32 max_health;
	f32 current_health;
	char name[FORGE_NAME_LEN];
} forge_boss_state_t;

/* ============================================================
 * Objective (F7)
 * ============================================================ */

typedef struct forge_objective {
	u8 in_use;
	u8 kind;                             /* forge_objective_kind_t */
	u8 status;                           /* forge_objective_status_t */
	u8 order;                            /* for sequential */
	u32 completion_node_uid;             /* logic node that completes this */
	u32 failure_node_uid;                /* logic node that fails this */
	char description[FORGE_TEXT_LEN];
} forge_objective_t;

/* ============================================================
 * Prefab (F2 §4.3)
 * ============================================================ */

typedef struct forge_prefab {
	u8 in_use;
	u8 num_objects;
	u8 pad[2];
	char name[FORGE_NAME_LEN];
	char author[FORGE_NAME_LEN];
	forge_object_t objects[32];          /* up to 32 objects per prefab */
} forge_prefab_t;

/* ============================================================
 * Lighting / atmosphere (F4 §8)
 * ============================================================ */

typedef struct forge_skylight {
	f32 direction_yaw_deg;
	f32 direction_pitch_deg;
	f32 color[3];
	f32 intensity;
	u8 cast_shadow;
	u8 shadow_softness;
	u8 pad[2];
} forge_skylight_t;

typedef struct forge_atmosphere {
	u8 fog_enable;
	u8 bloom_enable;
	u8 color_grade;                      /* 0=neutral,1=warm,2=cool,3=noir,4=alien */
	u8 pad;
	f32 fog_color[3];
	f32 fog_near;
	f32 fog_far;
	f32 fog_height;
	f32 ambient_color[3];
	f32 ambient_intensity;
	f32 exposure;
	f32 bloom_strength;
	char sky_id[FORGE_ID_LEN];           /* catalog sky id */
	/* F8 weather stretch */
	u8 weather_kind;                     /* 0=clear,1=rain,2=snow,3=sandstorm,4=storm */
	u8 weather_intensity;
	u8 pad_w[2];
	/* F8 time of day stretch */
	u8 tod_enable;
	u8 pad_tod[3];
	f32 tod_cycle_sec;                   /* duration of full day/night cycle */
	f32 tod_current_phase;               /* 0..1 */
} forge_atmosphere_t;

/* ============================================================
 * Map header / settings (F3 §9)
 * ============================================================ */

typedef struct forge_map_settings {
	char map_name[FORGE_NAME_LEN];
	char author[FORGE_NAME_LEN];
	char description[FORGE_DESC_LEN];
	char base_stage_id[FORGE_ID_LEN];    /* base stage catalog id */
	/* game modes supported (bitfield of gamemode bits) */
	u32 gamemode_flags;
	u8 max_players;
	u8 recommended_players;
	u8 map_size_tag;                     /* 0=small,1=med,2=large */
	u8 pad_tag;
	/* spawn config */
	u8 team_spawn_mode;                  /* 0=scattered,1=zoned,2=symmetric */
	u8 use_initial_only;
	u8 pad_sc[2];
	s32 min_spawn_points;
	f32 respawn_delay_sec;
	f32 spawn_protection_sec;
	/* game rules defaults */
	s32 default_time_limit_sec;
	s32 default_score_limit;
	u8 health_setting;                   /* 0=normal,1=200%,2=50% */
	u8 radar_setting;                    /* 0=on,1=off,2=proximity */
	u8 auto_aim;
	u8 friendly_fire;
	u8 one_hit_kills;
	u8 pad_rules[3];
	/* bounds */
	f32 bounds_min[3];
	f32 bounds_max[3];
	f32 soft_bounds_timer_sec;
	/* F7 mission */
	char briefing_text[FORGE_DESC_LEN];
	char debrief_text[FORGE_DESC_LEN];
	u8 is_mission;
	u8 sequential_objectives;
	u8 pad_mission[2];
	/* grid/snap prefs */
	f32 grid_size;
	f32 rotation_snap_deg;
	u8 surface_snap;
	u8 edge_snap;
	u8 pad_snap[2];

	/* S310 R2/R4 -- weapon pad source policy (see forge_weapon_source_t).
	 * Default is MAP_DEFAULTS so author intent is honoured unless the
	 * match explicitly overrides. */
	u8 weapon_source;
	u8 allow_match_override; /* 1 = match-setup UI shows "Use Map Defaults"
	                          *     checkbox next to weapon-set picker */
	u8 pad_weaponsrc[2];

	/* S313 -- Map variant metadata.  variant_mode picks whether this
	 * save is a blank-canvas map or a delta on top of another map /
	 * engine stage.  `variant_source_slug` is meaningful when
	 * variant_mode == EDIT_MAP and points at another Grid mod slug.
	 * EDIT_STAGE reuses the existing `base_stage_id` field. */
	u8 variant_mode;                     /* forge_map_variant_mode_t */
	u8 pad_variant[3];
	char variant_source_slug[FORGE_NAME_LEN];
} forge_map_settings_t;

/* S313 -- Author-side live bot testing settings.  Exposed via the
 * "Bots" tab so mid-test the author can add/remove bots without a
 * match restart.  Runtime hook to engine botmgr is deferred; today
 * the data model captures intent and logs actions. */
typedef struct forge_bot_settings {
	u8 spawn_mode;                       /* forge_bot_spawn_mode_t */
	u8 active_count;                     /* desired bots fighting */
	u8 frozen_count;                     /* desired bots frozen for spawn-spot testing */
	u8 all_frozen;                       /* global "freeze all" toggle */
	f32 near_me_radius;                  /* when spawn_mode==NEAR_ME */
	f32 smart_aggression;                /* when spawn_mode==SMART (0..1) */
	char default_body_id[FORGE_ID_LEN];  /* preferred body for added bots */
	char default_difficulty[FORGE_NAME_LEN]; /* "meat","easy","normal","hard","perfect","dark" */
	s32 pending_add_active;              /* runtime counter: +1 per Add Bot click (active) */
	s32 pending_add_frozen;              /* runtime counter: +1 per Add Bot click (frozen) */
	s32 pending_remove_all;              /* runtime counter: +1 per Remove All click */
} forge_bot_settings_t;

/* ============================================================
 * Placement / editor state
 * ============================================================ */

typedef enum forge_tool {
	FORGE_TOOL_SELECT = 0,
	FORGE_TOOL_PLACE,
	FORGE_TOOL_TRANSLATE,
	FORGE_TOOL_ROTATE,
	FORGE_TOOL_SCALE,
	FORGE_TOOL_LINK,                     /* F5 quick-link */
	FORGE_TOOL_COUNT
} forge_tool_t;

typedef enum forge_edit_target {
	FORGE_TARGET_OBJECTS = 0,
	FORGE_TARGET_LOGIC,
	FORGE_TARGET_GAMETYPE,
	FORGE_TARGET_MISSION,
	FORGE_TARGET_LIGHTING,
	FORGE_TARGET_SETTINGS,
	FORGE_TARGET_COUNT
} forge_edit_target_t;

typedef struct forge_placement_state {
	u8 ghost_active;                     /* ghost reticle visible */
	u8 ghost_valid;                      /* green tint vs red */
	u8 pad[2];
	char pending_catalog_id[FORGE_ID_LEN];
	f32 ghost_pos[3];
	f32 ghost_rot[3];
	f32 ghost_scale[3];
} forge_placement_state_t;

typedef struct forge_editor_state {
	forge_tool_t tool;
	forge_edit_target_t target;
	u8 snap_grid_enabled;
	u8 snap_surface_enabled;
	u8 snap_edge_enabled;
	u8 paused_sim;                       /* forge pauses gameplay simulation */
	u8 zone_viz_enabled;                 /* V key */
	u8 logic_wires_viz_enabled;          /* L key */
	u8 category_filter;                  /* forge_category_t or FORGE_CAT_COUNT for all */
	u8 budget_warning_shown;
	f32 grid_size;
	f32 rotation_snap_deg;
	char search_filter[FORGE_NAME_LEN];
} forge_editor_state_t;

typedef struct forge_undo_entry {
	u8 in_use;
	u8 op_kind;                          /* 0=place,1=delete,2=transform,3=prop,4=undo_barrier */
	u8 pad[2];
	u32 uid;
	/* snapshot of the object before / after; op_kind determines which is meaningful */
	forge_object_t before;
	forge_object_t after;
} forge_undo_entry_t;

/* ============================================================
 * Public API
 * ============================================================ */

/* Utility */
void forgeCopyStr(char *dst, const char *src, size_t n);

/* Lifecycle */
void forgeCoreInit(void);
void forgeCoreReset(void);                       /* clears all map data */
void forgeCoreTick(void);                        /* per-frame runtime update (logic, waves, etc.) */

/* Catalog (F1) */
s32  forgeCatalogCount(void);
const forge_catalog_entry_t *forgeCatalogGet(s32 index);
const forge_catalog_entry_t *forgeCatalogFind(const char *id);
s32  forgeCatalogSearch(const char *query, s32 *out_indices, s32 max);
const char *forgeCategoryName(forge_category_t c);

/* Object store (F1) */
s32  forgeObjectCount(void);
forge_object_t *forgeObjectGet(s32 index);
forge_object_t *forgeObjectFindByUid(u32 uid);
forge_object_t *forgeObjectAllocate(forge_category_t cat, const char *catalog_id);
void forgeObjectRemove(u32 uid);
u32  forgeObjectNextUid(void);

/* Selection (F1/F2) */
s32  forgeSelectionCount(void);
u32  forgeSelectionGet(s32 index);
void forgeSelectionAdd(u32 uid);
void forgeSelectionRemove(u32 uid);
void forgeSelectionClear(void);
void forgeSelectionSelectOnly(u32 uid);
void forgeSelectionDelete(void);                  /* delete all selected */
void forgeSelectionDuplicate(f32 offset_x, f32 offset_y, f32 offset_z);

/* Placement (F1) */
forge_placement_state_t *forgeGetPlacement(void);
void forgePlaceBegin(const char *catalog_id);
void forgePlaceUpdate(const f32 camera_pos[3], f32 camera_yaw_deg, f32 camera_pitch_deg, f32 distance);
s32  forgePlaceCommit(void);                      /* returns uid (0 on failure) */
void forgePlaceCancel(void);

/* Held object (Fix 8, 2026-05-01).  When the user picks a catalog entry the
 * object spawns immediately and is "held" -- its position tracks the freefly
 * camera every tick (camera + forward * distance) until the user presses
 * Activate (A on pad / D-pad RIGHT) again, which releases it.  X / Tab while
 * holding switches the editor to the Properties context for the held object
 * instead of toggling editor visibility.  forgeHeldGetUid returns 0 when
 * nothing is held; otherwise the uid of the live forge_object_t. */
u32  forgeHeldGetUid(void);
void forgeHeldSetUid(u32 uid);
void forgeHeldRelease(void);
void forgeHeldUpdateFromCamera(const f32 camera_pos[3], f32 camera_yaw_deg, f32 camera_pitch_deg, f32 distance);

/* Editor state (F1+) */
forge_editor_state_t *forgeGetEditor(void);

/* Undo/redo (F1) */
s32  forgeUndoCanUndo(void);
s32  forgeUndoCanRedo(void);
void forgeUndoPushBarrier(const char *label);
void forgeUndoRecordPlace(u32 uid, const forge_object_t *snap);
void forgeUndoRecordDelete(u32 uid, const forge_object_t *snap);
void forgeUndoRecordTransform(u32 uid, const forge_object_t *before, const forge_object_t *after);
void forgeUndoRecordPropChange(u32 uid, const forge_object_t *before, const forge_object_t *after);
void forgeUndoApplyUndo(void);
void forgeUndoApplyRedo(void);

/* Prefabs (F2) */
s32  forgePrefabCount(void);
forge_prefab_t *forgePrefabGet(s32 index);
forge_prefab_t *forgePrefabSaveFromSelection(const char *name);
s32  forgePrefabInstantiate(s32 prefab_index, const f32 origin[3]);

/* Logic graph (F5) */
s32  forgeLogicNodeCount(void);
forge_logic_node_t *forgeLogicNodeGet(s32 index);
forge_logic_node_t *forgeLogicNodeFindByUid(u32 uid);
forge_logic_node_t *forgeLogicNodeAllocate(forge_logic_kind_t kind, forge_logic_op_t op);
void forgeLogicNodeRemove(u32 uid);
s32  forgeLogicWireCount(void);
forge_logic_wire_t *forgeLogicWireGet(s32 index);
s32  forgeLogicWireCreate(u32 src_uid, u32 dst_uid, u8 src_port, u8 dst_port);
void forgeLogicWireRemove(s32 index);
s32  forgeChannelCount(void);
forge_channel_t *forgeChannelGet(s32 index);
forge_channel_t *forgeChannelFind(const char *name);
forge_channel_t *forgeChannelCreate(const char *name);
void forgeChannelSet(const char *name, u8 state);
void forgeLogicFireEvent(forge_logic_op_t event_op, u32 related_uid);
void forgeLogicDetectCycles(s32 *out_warn_count);

/* Game type (F5b) */
forge_gametype_t *forgeGameType(void);
void forgeGameTypeReset(void);
forge_boss_state_t *forgeBossState(void);
void forgeBossSetActive(u32 chr_uid, const char *name, f32 max_health, f32 *phase_thresholds, s32 num_thresholds);
void forgeBossApplyDamage(u32 chr_uid, f32 amount);

/* Objectives (F7) */
s32  forgeObjectiveCount(void);
forge_objective_t *forgeObjectiveGet(s32 index);
forge_objective_t *forgeObjectiveAllocate(forge_objective_kind_t kind);
void forgeObjectiveRemove(s32 index);
void forgeObjectiveSetStatus(s32 index, forge_objective_status_t status);

/* Atmosphere / skylight (F4) */
forge_skylight_t *forgeSkylight(void);
forge_atmosphere_t *forgeAtmosphere(void);

/* Map settings (F3) */
forge_map_settings_t *forgeMapSettings(void);

/* S313 -- Live bot testing (author-side data + log-only runtime today;
 * engine botmgr wire is a follow-up polish pass). */
forge_bot_settings_t *forgeBotSettings(void);
void forgeBotAddRequest(s32 active);  /* active != 0 -> fighting bot; else frozen */
void forgeBotRemoveAll(void);
void forgeBotFreezeAll(s32 frozen);    /* 0 = unfreeze, 1 = freeze */

/* S313 -- Map variant helpers. */
void forgeImportBaseStageObjects(void); /* marks current object pool as from_base */
s32  forgeObjectResetToBase(u32 uid);   /* revert an object to its base snapshot */
s32  forgeObjectRemoveFromBase(u32 uid);/* delete a base object (variant-side delete) */
s32  forgeCountBaseObjects(void);       /* number of objects with from_base != 0 */
s32  forgeCountDeltaObjects(void);      /* number of author-only objects */

/* Budget reporting (F1) */
typedef struct forge_budget_stats {
	s32 objects;
	s32 objects_soft;
	s32 objects_hard;
	s32 triangles;
	s32 triangles_soft;
	s32 triangles_hard;
	s32 lights;
	s32 lights_soft;
	s32 lights_hard;
	s32 logic_nodes;
	s32 logic_nodes_soft;
	s32 logic_nodes_hard;
	s32 effects;
	s32 effects_soft;
	s32 effects_hard;
	s32 audio_emitters;
	s32 audio_emitters_soft;
	s32 audio_emitters_hard;
} forge_budget_stats_t;

void forgeBudgetCompute(forge_budget_stats_t *out);
s32  forgeBudgetOverSoft(const forge_budget_stats_t *s);
s32  forgeBudgetOverHard(const forge_budget_stats_t *s);

/* Serialize (F3) */
s32  forgeSerializeSaveToMod(const char *mod_slug); /* writes mods/Forge Maps/<slug>/ */
s32  forgeSerializeLoadFromMod(const char *mod_slug);

/* ============================================================
 * S310 R3: Mod dependency collection.
 *
 * Walks every piece of editor state -- placed objects' catalog IDs,
 * AI body/head/weapon refs, weapon pad weapon_id, pickup item_id,
 * effect asset_id, zone sound_loop_id, atmosphere sky_id, gametype
 * starting_weapon, every wave's enemy_catalog_id, plus any
 * material_id on any object -- and returns the unique list of
 * **non-base** catalog IDs referenced.  "non-base" = any ID whose
 * namespace prefix is not "base:".  Those are the mod assets the map
 * depends on and that the distribution pipeline needs to recursively
 * include (a modded weapon's own texture-pack dependency comes along
 * too because each mod declares its own dependency list in its mod.json).
 *
 * The caller supplies a char[FORGE_ID_LEN] array of at least `max`
 * entries; returns the count of unique IDs actually written (capped
 * at `max`).  Base-game IDs are elided; NULL / empty IDs are elided.
 * ============================================================ */
s32  forgeCollectDependencies(char out[][FORGE_ID_LEN], s32 max);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_FORGE_CORE_H */
