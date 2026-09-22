/**
 * theater_format.h -- bounded, restart-safe Perfect Dark Theater container.
 *
 * The public structs below are input/output values only. The file codec writes
 * every field explicitly in little-endian order; it never dumps a C layout,
 * pointer, runtime asset index, or renderer/cache state. Version 2 stores
 * uncompressed full checkpoints and is deliberately bounded to 512 MiB; full
 * campaign retention still requires a compression/delta unit and a persistent
 * reader.
 */

#ifndef PD_THEATER_FORMAT_H
#define PD_THEATER_FORMAT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEATER_FORMAT_MAGIC "PDTH"
#define THEATER_FORMAT_VERSION 2u
#define THEATER_FORMAT_HEADER_SIZE 64u
#define THEATER_FORMAT_RECORD_HEADER_SIZE 32u

#define THEATER_FORMAT_ID_MAX 64u
#define THEATER_FORMAT_NAME_MAX 32u
#define THEATER_FORMAT_CATEGORY_MAX 32u
#define THEATER_FORMAT_VERSION_ID_MAX 32u
#define THEATER_FORMAT_MAX_ROSTER 40u
#define THEATER_FORMAT_MAX_MANIFEST_ENTRIES 4096u
#define THEATER_FORMAT_MAX_VIEWS THEATER_FORMAT_MAX_ROSTER
#define THEATER_FORMAT_MAX_ENTITIES 4096u
#define THEATER_FORMAT_MAX_RECORD_PAYLOAD (8u * 1024u * 1024u)
#define THEATER_FORMAT_MAX_FILE_SIZE (512u * 1024u * 1024u)
#define THEATER_FORMAT_MAX_RECORDS 4000000u
#define THEATER_FORMAT_MAX_CHECKPOINTS 65536u
#define THEATER_FORMAT_WEAPON_SLOTS 6u

/* COMPLETE means the current container has a validated seek index and END.
 * RECOVERED additionally means those terminal records were reconstructed from
 * an interrupted accepted prefix; it is invalid without COMPLETE. */
#define THEATER_FORMAT_FLAG_COMPLETE 0x00000001u
#define THEATER_FORMAT_FLAG_RECOVERED 0x00000002u

typedef enum theater_format_result_e {
	THEATER_FORMAT_OK = 0,
	THEATER_FORMAT_INVALID_ARGUMENT,
	THEATER_FORMAT_INVALID_IDENTITY,
	THEATER_FORMAT_NOT_AUTHORITY,
	THEATER_FORMAT_IO_ERROR,
	THEATER_FORMAT_UNSUPPORTED_VERSION,
	THEATER_FORMAT_TRUNCATED,
	THEATER_FORMAT_CORRUPT,
	THEATER_FORMAT_OVERSIZED,
	THEATER_FORMAT_LIMIT_EXCEEDED,
	THEATER_FORMAT_INCOMPLETE,
	THEATER_FORMAT_NOT_FOUND,
} theater_format_result_t;

typedef enum theater_format_mode_e {
	THEATER_FORMAT_MODE_CAMPAIGN = 1,
	THEATER_FORMAT_MODE_COMBAT_SIMULATOR = 2,
} theater_format_mode_t;

typedef enum theater_format_authority_e {
	THEATER_FORMAT_AUTHORITY_OFFLINE = 1,
	THEATER_FORMAT_AUTHORITY_LISTEN = 2,
} theater_format_authority_t;

typedef enum theater_format_campaign_variant_e {
	THEATER_FORMAT_CAMPAIGN_NONE = 0,
	THEATER_FORMAT_CAMPAIGN_SOLO = 1,
	THEATER_FORMAT_CAMPAIGN_COOPERATIVE = 2,
	THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE = 3,
} theater_format_campaign_variant_t;

typedef enum theater_format_record_kind_e {
	THEATER_FORMAT_RECORD_MATCH = 1,
	THEATER_FORMAT_RECORD_MANIFEST = 2,
	THEATER_FORMAT_RECORD_ROSTER = 3,
	THEATER_FORMAT_RECORD_CHECKPOINT = 4,
	THEATER_FORMAT_RECORD_EVENT = 5,
	THEATER_FORMAT_RECORD_SEEK_INDEX = 6,
	THEATER_FORMAT_RECORD_END = 7,
} theater_format_record_kind_t;

#define THEATER_FORMAT_MANIFEST_REQUIRED 0x0001u
#define THEATER_FORMAT_MANIFEST_HAS_DIGEST 0x0002u
#define THEATER_FORMAT_MANIFEST_HAS_VERSION 0x0004u

/* Stable v2 values mirror the authoritative match-manifest type domain. */
typedef enum theater_format_manifest_type_e {
	THEATER_FORMAT_MANIFEST_BODY = 0,
	THEATER_FORMAT_MANIFEST_HEAD = 1,
	THEATER_FORMAT_MANIFEST_STAGE = 2,
	THEATER_FORMAT_MANIFEST_WEAPON = 3,
	THEATER_FORMAT_MANIFEST_COMPONENT = 4,
	THEATER_FORMAT_MANIFEST_MODEL = 5,
	THEATER_FORMAT_MANIFEST_ANIMATION = 6,
	THEATER_FORMAT_MANIFEST_TEXTURE = 7,
	THEATER_FORMAT_MANIFEST_LANGUAGE = 8,
	THEATER_FORMAT_MANIFEST_AUDIO = 9,
	THEATER_FORMAT_MANIFEST_PROJECTILE = 10,
	THEATER_FORMAT_MANIFEST_ENTITY = 11,
	THEATER_FORMAT_MANIFEST_GENERIC_ASSET = 12,
} theater_format_manifest_type_t;

#define THEATER_FORMAT_MANIFEST_SLOT_MATCH 0xffffu
/* Stable generic-asset slot values mirror asset_type_e in v2. */
#define THEATER_FORMAT_CATALOG_TYPE_MISSION 11u
#define THEATER_FORMAT_CATALOG_TYPE_GAMEMODE 19u
#define THEATER_FORMAT_CATALOG_TYPE_BOT_PROFILE 25u
#define THEATER_FORMAT_CATALOG_TYPE_MAX 31u

#define THEATER_FORMAT_VIEW_ACTIVE 0x0001u
#define THEATER_FORMAT_VIEW_LOCAL 0x0002u
#define THEATER_FORMAT_VIEW_DERIVED_ORIENTATION 0x0004u

typedef enum theater_format_camera_mode_e {
	THEATER_FORMAT_CAMERA_FIRST_PERSON = 1,
	THEATER_FORMAT_CAMERA_THIRD_PERSON = 2,
	THEATER_FORMAT_CAMERA_EYESPY = 3,
} theater_format_camera_mode_t;

typedef enum theater_format_spawn_weapon_mode_e {
	THEATER_FORMAT_SPAWN_WEAPON_NONE = 0,
	THEATER_FORMAT_SPAWN_WEAPON_SPECIFIC = 1,
	THEATER_FORMAT_SPAWN_WEAPON_RANDOM = 2,
	THEATER_FORMAT_SPAWN_WEAPON_FIESTA = 3,
} theater_format_spawn_weapon_mode_t;

typedef enum theater_format_participant_kind_e {
	THEATER_FORMAT_PARTICIPANT_LOCAL = 1,
	THEATER_FORMAT_PARTICIPANT_REMOTE = 2,
	THEATER_FORMAT_PARTICIPANT_BOT = 3,
} theater_format_participant_kind_t;

typedef enum theater_format_participant_role_e {
	THEATER_FORMAT_ROLE_NONE = 0,
	THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY = 1,
	THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE = 2,
	THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE = 3,
} theater_format_participant_role_t;

typedef enum theater_format_entity_kind_e {
	THEATER_FORMAT_ENTITY_PLAYER = 1,
	THEATER_FORMAT_ENTITY_BOT = 2,
	THEATER_FORMAT_ENTITY_NPC = 3,
	THEATER_FORMAT_ENTITY_OBJECT = 4,
	THEATER_FORMAT_ENTITY_DOOR = 5,
	THEATER_FORMAT_ENTITY_LIFT = 6,
	THEATER_FORMAT_ENTITY_WEAPON = 7,
	THEATER_FORMAT_ENTITY_PROJECTILE = 8,
} theater_format_entity_kind_t;

#define THEATER_FORMAT_ENTITY_STATE_DEAD 0x0001u

typedef enum theater_format_door_motion_e {
	THEATER_FORMAT_DOOR_NONE = 0,
	THEATER_FORMAT_DOOR_IDLE = 1,
	THEATER_FORMAT_DOOR_OPENING = 2,
	THEATER_FORMAT_DOOR_CLOSING = 3,
	THEATER_FORMAT_DOOR_WAITING = 4,
} theater_format_door_motion_t;

typedef enum theater_format_event_kind_e {
	THEATER_FORMAT_EVENT_STAGE_BEGIN = 1,
	THEATER_FORMAT_EVENT_OBJECTIVE = 2,
	THEATER_FORMAT_EVENT_CUTSCENE = 3,
	THEATER_FORMAT_EVENT_SCORE = 4,
	THEATER_FORMAT_EVENT_ENTITY_SPAWN = 5,
	THEATER_FORMAT_EVENT_ENTITY_RETIRE = 6,
	THEATER_FORMAT_EVENT_MATCH_END = 7,
	THEATER_FORMAT_EVENT_STAGE_FLAGS = 8,
} theater_format_event_kind_t;

typedef struct theater_format_match_s {
	u8 mode;
	u8 authority;
	u8 difficulty;
	u8 campaign_variant;
	u32 tick_rate;
	u32 checkpoint_interval_ticks;
	u32 options;
	u32 time_limit;
	u32 score_limit;
	u32 team_score_limit;
	u64 start_time_unix;
	u8 manifest_digest[32];
	char stage_id[THEATER_FORMAT_ID_MAX];
	char mode_id[THEATER_FORMAT_ID_MAX];
	char mission_id[THEATER_FORMAT_ID_MAX];
	char weapon_ids[THEATER_FORMAT_WEAPON_SLOTS][THEATER_FORMAT_ID_MAX];
	char spawn_weapon_id[THEATER_FORMAT_ID_MAX];
	u8 spawn_weapon_mode;
	u8 reserved[3];
} theater_format_match_t;

typedef struct theater_format_roster_entry_s {
	u16 slot;
	u8 kind;
	u8 team;
	u8 role;
	u8 reserved[3];
	char name[THEATER_FORMAT_NAME_MAX];
	char body_id[THEATER_FORMAT_ID_MAX];
	char head_id[THEATER_FORMAT_ID_MAX];
	char profile_id[THEATER_FORMAT_ID_MAX];
} theater_format_roster_entry_t;

typedef struct theater_format_manifest_entry_s {
	u16 type;
	u16 slot;
	u16 flags;
	u16 reserved;
	u8 content_digest[32];
	char catalog_id[THEATER_FORMAT_ID_MAX];
	char category[THEATER_FORMAT_CATEGORY_MAX];
	char version_id[THEATER_FORMAT_VERSION_ID_MAX];
} theater_format_manifest_entry_t;

typedef struct theater_format_entity_s {
	u32 stable_id;
	u32 parent_id;
	u16 kind;
	u16 state_flags; /* THEATER_FORMAT_ENTITY_STATE_* only */
	s32 room;
	f32 health;
	f32 position[3];
	f32 orientation[9]; /* row-major 3x3 world orientation */
	f32 velocity[3];
	s32 score;
	s32 deaths;
	f32 door_fraction;
	f32 door_speed;
	u8 door_motion;
	u8 reserved[3];
	char asset_id[THEATER_FORMAT_ID_MAX];
	char secondary_asset_id[THEATER_FORMAT_ID_MAX];
} theater_format_entity_t;

typedef struct theater_format_view_s {
	u16 slot;
	u16 flags;
	u32 entity_id;
	s32 camera_mode;
	f32 position[3];
	f32 forward[3];
	f32 up[3];
	f32 fov_y_degrees;
	f32 aspect;
	f32 aim_yaw_degrees;
	f32 aim_pitch_degrees;
} theater_format_view_t;

typedef struct theater_format_checkpoint_s {
	u64 tick;
	u64 elapsed_ms;
	u64 stage_flags;
	u64 objective_flags;
	u32 match_elapsed_ticks;
	u32 entity_count;
	const theater_format_entity_t *entities;
	u32 view_count;
	const theater_format_view_t *views;
} theater_format_checkpoint_t;

typedef struct theater_format_event_s {
	u64 tick;
	u16 kind;
	u16 reserved;
	u32 actor_id;
	u32 target_id;
	s32 value[4];
	char catalog_id[THEATER_FORMAT_ID_MAX];
} theater_format_event_t;

typedef struct theater_format_index_entry_s {
	u64 tick;
	u64 file_offset;
	u32 sequence;
	u32 record_crc;
} theater_format_index_entry_t;

typedef struct theater_format_info_s {
	u32 version;
	u32 flags;
	u64 file_size;
	u32 record_count;
	u32 checkpoint_count;
	u32 roster_count;
	u32 manifest_count;
	u64 index_offset;
	u32 index_length;
	theater_format_match_t match;
} theater_format_info_t;

typedef struct theater_format_writer_s theater_format_writer_t;

typedef struct theater_format_mark_s {
	u64 file_offset;
	u64 last_tick;
	u32 record_count;
	u32 checkpoint_count;
	u32 next_sequence;
	s32 has_tick;
} theater_format_mark_t;

theater_format_writer_t *theaterFormatBegin(
	const char *final_path,
	const theater_format_match_t *match,
	const theater_format_manifest_entry_t *manifest,
	u32 manifest_count,
	const theater_format_roster_entry_t *roster,
	u32 roster_count,
	theater_format_result_t *out_result);

theater_format_result_t theaterFormatAppendCheckpoint(
	theater_format_writer_t *writer,
	const theater_format_checkpoint_t *checkpoint);

theater_format_result_t theaterFormatAppendEvent(
	theater_format_writer_t *writer,
	const theater_format_event_t *event);

theater_format_result_t theaterFormatMark(
	theater_format_writer_t *writer, theater_format_mark_t *out_mark);
theater_format_result_t theaterFormatRollback(
	theater_format_writer_t *writer, const theater_format_mark_t *mark);

theater_format_result_t theaterFormatFinish(theater_format_writer_t *writer);

/* Delete the incomplete candidate. */
void theaterFormatAbort(theater_format_writer_t *writer);

/* Close while deliberately retaining <final>.part for crash-recovery tests. */
void theaterFormatAbandon(theater_format_writer_t *writer);

/* Validate a completed file without publishing partial output on failure. */
theater_format_result_t theaterFormatValidate(
	const char *path,
	theater_format_info_t *out_info);

/* Recover the maximal CRC-valid prefix only after a physical terminal tear.
 * Any fully present record with a CRC or semantic failure remains corrupt. */
theater_format_result_t theaterFormatRecoverInterrupted(const char *final_path);

/* Load and validate the complete deterministic checkpoint index. */
theater_format_result_t theaterFormatReadIndex(
	const char *path,
	theater_format_index_entry_t *entries,
	u32 capacity,
	u32 *out_count);

/* Full validation occurs before either output buffer is published. */
theater_format_result_t theaterFormatReadManifest(
	const char *path,
	theater_format_manifest_entry_t *entries,
	u32 capacity,
	u32 *out_count);

theater_format_result_t theaterFormatReadRoster(
	const char *path,
	theater_format_roster_entry_t *entries,
	u32 capacity,
	u32 *out_count);

theater_format_result_t theaterFormatReadCheckpoint(
	const char *path,
	u32 checkpoint_index,
	theater_format_checkpoint_t *out_checkpoint,
	theater_format_entity_t *entities,
	u32 entity_capacity,
	theater_format_view_t *views,
	u32 view_capacity);

const char *theaterFormatResultString(theater_format_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PD_THEATER_FORMAT_H */
