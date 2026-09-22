/**
 * scenario_codec.h -- Strict, globals-free saved Scenario JSON decoder.
 *
 * Parsing produces an immutable candidate document. Catalog resolution,
 * legacy migration, and live match publication are deliberately separate so
 * malformed or semantically invalid input cannot mutate runtime state.
 */

#ifndef SCENARIO_CODEC_H
#define SCENARIO_CODEC_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifndef SCENARIO_CODEC_ID_LEN
#define SCENARIO_CODEC_ID_LEN 64
#endif

#ifndef SCENARIO_CODEC_NAME_LEN
#define SCENARIO_CODEC_NAME_LEN 64
#endif

#ifndef SCENARIO_CODEC_BOT_NAME_LEN
#define SCENARIO_CODEC_BOT_NAME_LEN 32
#endif

#ifndef SCENARIO_CODEC_WEAPON_SLOTS
#define SCENARIO_CODEC_WEAPON_SLOTS 6
#endif

#ifndef SCENARIO_CODEC_MAX_BOTS
#define SCENARIO_CODEC_MAX_BOTS 32
#endif

typedef enum scenario_codec_status_e {
	SCENARIO_CODEC_OK = 0,
	SCENARIO_CODEC_INVALID_ARGUMENT,
	SCENARIO_CODEC_EMPTY,
	SCENARIO_CODEC_TOO_LARGE,
	SCENARIO_CODEC_INVALID_JSON,
	SCENARIO_CODEC_DUPLICATE_FIELD,
	SCENARIO_CODEC_UNKNOWN_FIELD,
	SCENARIO_CODEC_WRONG_TYPE,
	SCENARIO_CODEC_VALUE_OUT_OF_RANGE,
	SCENARIO_CODEC_STRING_TOO_LONG,
	SCENARIO_CODEC_UNSUPPORTED_VERSION,
	SCENARIO_CODEC_MISSING_FIELD,
	SCENARIO_CODEC_TOO_MANY_BOTS,
} scenario_codec_status_e;

typedef struct scenario_document_bot_t {
	u32 present;
	char name[SCENARIO_CODEC_BOT_NAME_LEN];
	char profile_id[SCENARIO_CODEC_ID_LEN];
	char body_id[SCENARIO_CODEC_ID_LEN];
	char head_id[SCENARIO_CODEC_ID_LEN];
	s32 difficulty;
	s32 legacy_body;
	s32 legacy_head;
} scenario_document_bot_t;

enum scenario_document_bot_field_e {
	SCENARIO_BOT_NAME = 1u << 0,
	SCENARIO_BOT_PROFILE_ID = 1u << 1,
	SCENARIO_BOT_DIFFICULTY = 1u << 2,
	SCENARIO_BOT_BODY_ID = 1u << 3,
	SCENARIO_BOT_HEAD_ID = 1u << 4,
	SCENARIO_BOT_LEGACY_BODY = 1u << 5,
	SCENARIO_BOT_LEGACY_HEAD = 1u << 6,
};

typedef struct scenario_document_t {
	u32 present;
	s32 version;
	char name[SCENARIO_CODEC_NAME_LEN];
	char arena_id[SCENARIO_CODEC_ID_LEN];
	char scenario_id[SCENARIO_CODEC_ID_LEN];
	s32 legacy_arena;
	s32 legacy_scenario;
	s32 timelimit;
	s32 scorelimit;
	u32 teamscorelimit;
	u32 options;
	s32 weaponset;
	char weapon_ids[SCENARIO_CODEC_WEAPON_SLOTS][SCENARIO_CODEC_ID_LEN];
	u8 weapon_id_present[SCENARIO_CODEC_WEAPON_SLOTS];
	s32 legacy_weapons[SCENARIO_CODEC_WEAPON_SLOTS];
	u8 legacy_weapon_present[SCENARIO_CODEC_WEAPON_SLOTS];
	char spawn_weapon_id[SCENARIO_CODEC_ID_LEN];
	s32 spawn_weapon_mode;
	s32 bot_count;
	scenario_document_bot_t bots[SCENARIO_CODEC_MAX_BOTS];
} scenario_document_t;

enum scenario_document_field_e {
	SCENARIO_DOC_VERSION = 1u << 0,
	SCENARIO_DOC_NAME = 1u << 1,
	SCENARIO_DOC_ARENA_ID = 1u << 2,
	SCENARIO_DOC_SCENARIO_ID = 1u << 3,
	SCENARIO_DOC_LEGACY_ARENA = 1u << 4,
	SCENARIO_DOC_LEGACY_SCENARIO = 1u << 5,
	SCENARIO_DOC_TIMELIMIT = 1u << 6,
	SCENARIO_DOC_SCORELIMIT = 1u << 7,
	SCENARIO_DOC_TEAMSCORELIMIT = 1u << 8,
	SCENARIO_DOC_OPTIONS = 1u << 9,
	SCENARIO_DOC_WEAPONSET = 1u << 10,
	SCENARIO_DOC_SPAWN_WEAPON_ID = 1u << 11,
	SCENARIO_DOC_SPAWN_WEAPON_MODE = 1u << 12,
	SCENARIO_DOC_BOTS = 1u << 13,
};

/** Maximum accepted serialized document size, excluding the trailing NUL. */
#define SCENARIO_CODEC_MAX_BYTES 65536u

/**
 * Decode one exact saved Scenario document.
 *
 * The decoder rejects malformed/trailing JSON, duplicate or unknown keys,
 * wrong value types, overflowing integers/strings, unsupported versions, and
 * version-specific writer shapes. V1 and v2 admit only their bounded historic
 * migration forms; when a v2 typed identity is present it is authoritative and
 * cannot fall back to its numeric companion. V3 accepts typed-only source plus
 * the exact prior shipping hybrid shape (all eight numeric cache companions);
 * semantic planning must prove companions agree. On failure, out remains zero.
 */
scenario_codec_status_e scenarioDocumentParse(const char *json, size_t length,
		scenario_document_t *out, char *detail, size_t detail_size);

const char *scenarioCodecStatusString(scenario_codec_status_e status);

#endif /* SCENARIO_CODEC_H */
