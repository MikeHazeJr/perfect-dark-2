#include "scenario_codec.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct scenario_json_reader_t {
	const char *json;
	size_t length;
	size_t pos;
	char *detail;
	size_t detail_size;
} scenario_json_reader_t;

static scenario_codec_status_e scenarioJsonFail(scenario_json_reader_t *reader,
		scenario_codec_status_e status, const char *format, ...)
{
	if (reader && reader->detail && reader->detail_size > 0) {
		va_list args;
		va_start(args, format);
		vsnprintf(reader->detail, reader->detail_size, format, args);
		va_end(args);
		reader->detail[reader->detail_size - 1] = '\0';
	}
	return status;
}

static void scenarioJsonSkipSpace(scenario_json_reader_t *reader)
{
	while (reader->pos < reader->length) {
		char c = reader->json[reader->pos];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
		reader->pos++;
	}
}

static scenario_codec_status_e scenarioJsonConsume(scenario_json_reader_t *reader,
		char expected)
{
	scenarioJsonSkipSpace(reader);
	if (reader->pos >= reader->length || reader->json[reader->pos] != expected) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
			"expected '%c' at byte %u", expected, (unsigned)reader->pos);
	}
	reader->pos++;
	return SCENARIO_CODEC_OK;
}

static scenario_codec_status_e scenarioJsonString(scenario_json_reader_t *reader,
		char *out, size_t out_size)
{
	size_t written = 0;
	scenario_codec_status_e status;

	if (!out || out_size == 0) return SCENARIO_CODEC_INVALID_ARGUMENT;
	out[0] = '\0';
	status = scenarioJsonConsume(reader, '"');
	if (status != SCENARIO_CODEC_OK) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_WRONG_TYPE,
			"expected string at byte %u", (unsigned)reader->pos);
	}

	while (reader->pos < reader->length) {
		unsigned char c = (unsigned char)reader->json[reader->pos++];
		if (c == '"') {
			out[written] = '\0';
			return SCENARIO_CODEC_OK;
		}
		if (c < 0x20) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
				"unescaped control byte in string");
		}
		if (c == '\\') {
			if (reader->pos >= reader->length) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
					"truncated string escape");
			}
			c = (unsigned char)reader->json[reader->pos++];
			switch (c) {
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case '/': c = '/'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'u':
				return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
					"unicode escapes are not supported by the saved Scenario schema");
			default:
				return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
					"invalid string escape");
			}
		}
		if (written + 1 >= out_size) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_STRING_TOO_LONG,
				"string exceeds %u bytes", (unsigned)(out_size - 1));
		}
		out[written++] = (char)c;
	}

	return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
		"unterminated string");
}

static scenario_codec_status_e scenarioJsonInteger(scenario_json_reader_t *reader,
		s64 minimum, s64 maximum, s64 *out)
{
	s64 value = 0;
	s32 negative = 0;
	s32 digits = 0;

	scenarioJsonSkipSpace(reader);
	if (reader->pos >= reader->length) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_WRONG_TYPE,
			"expected integer at end of input");
	}
	if (reader->json[reader->pos] == '-') {
		negative = 1;
		reader->pos++;
	}
	if (reader->pos >= reader->length
			|| reader->json[reader->pos] < '0'
			|| reader->json[reader->pos] > '9') {
		return scenarioJsonFail(reader, SCENARIO_CODEC_WRONG_TYPE,
			"expected integer at byte %u", (unsigned)reader->pos);
	}
	if (reader->json[reader->pos] == '0'
			&& reader->pos + 1 < reader->length
			&& reader->json[reader->pos + 1] >= '0'
			&& reader->json[reader->pos + 1] <= '9') {
		return scenarioJsonFail(reader, SCENARIO_CODEC_INVALID_JSON,
			"integer has a leading zero");
	}
	while (reader->pos < reader->length
			&& reader->json[reader->pos] >= '0'
			&& reader->json[reader->pos] <= '9') {
		s32 digit = reader->json[reader->pos++] - '0';
		if (value > (LLONG_MAX - digit) / 10) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_VALUE_OUT_OF_RANGE,
				"integer overflows signed 64-bit range");
		}
		value = value * 10 + digit;
		digits++;
	}
	if (digits == 0) return SCENARIO_CODEC_WRONG_TYPE;
	if (reader->pos < reader->length) {
		char c = reader->json[reader->pos];
		if (c == '.' || c == 'e' || c == 'E' || c == '+') {
			return scenarioJsonFail(reader, SCENARIO_CODEC_WRONG_TYPE,
				"non-integral number is not permitted");
		}
	}
	if (negative) value = -value;
	if (value < minimum || value > maximum) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_VALUE_OUT_OF_RANGE,
			"integer is outside [%lld,%lld]", (long long)minimum,
			(long long)maximum);
	}
	*out = value;
	return SCENARIO_CODEC_OK;
}

static scenario_codec_status_e scenarioJsonMark(scenario_json_reader_t *reader,
		u32 *present, u32 bit, const char *field)
{
	if ((*present & bit) != 0) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_DUPLICATE_FIELD,
			"duplicate field '%s'", field);
	}
	*present |= bit;
	return SCENARIO_CODEC_OK;
}

static scenario_codec_status_e scenarioParseBot(scenario_json_reader_t *reader,
		scenario_document_bot_t *bot)
{
	scenario_codec_status_e status = scenarioJsonConsume(reader, '{');
	if (status != SCENARIO_CODEC_OK) return status;

	scenarioJsonSkipSpace(reader);
	if (reader->pos < reader->length && reader->json[reader->pos] == '}') {
		reader->pos++;
		return SCENARIO_CODEC_OK;
	}
	for (;;) {
		char key[32];
		s64 value;
		status = scenarioJsonString(reader, key, sizeof(key));
		if (status != SCENARIO_CODEC_OK) return status;
		status = scenarioJsonConsume(reader, ':');
		if (status != SCENARIO_CODEC_OK) return status;

		if (strcmp(key, "name") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_NAME, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
				bot->name, sizeof(bot->name));
		} else if (strcmp(key, "profileId") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_PROFILE_ID, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
				bot->profile_id, sizeof(bot->profile_id));
		} else if (strcmp(key, "bodyId") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_BODY_ID, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
				bot->body_id, sizeof(bot->body_id));
		} else if (strcmp(key, "headId") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_HEAD_ID, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
				bot->head_id, sizeof(bot->head_id));
		} else if (strcmp(key, "difficulty") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_DIFFICULTY, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
				INT_MIN, INT_MAX, &value);
			if (status == SCENARIO_CODEC_OK) bot->difficulty = (s32)value;
		} else if (strcmp(key, "body") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_LEGACY_BODY, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
				INT_MIN, INT_MAX, &value);
			if (status == SCENARIO_CODEC_OK) bot->legacy_body = (s32)value;
		} else if (strcmp(key, "head") == 0) {
			status = scenarioJsonMark(reader, &bot->present, SCENARIO_BOT_LEGACY_HEAD, key);
			if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
				INT_MIN, INT_MAX, &value);
			if (status == SCENARIO_CODEC_OK) bot->legacy_head = (s32)value;
		} else {
			return scenarioJsonFail(reader, SCENARIO_CODEC_UNKNOWN_FIELD,
				"unknown bot field '%s'", key);
		}
		if (status != SCENARIO_CODEC_OK) return status;
		scenarioJsonSkipSpace(reader);
		if (reader->pos >= reader->length) return SCENARIO_CODEC_INVALID_JSON;
		if (reader->json[reader->pos] == '}') {
			reader->pos++;
			return SCENARIO_CODEC_OK;
		}
		status = scenarioJsonConsume(reader, ',');
		if (status != SCENARIO_CODEC_OK) return status;
	}
}

static scenario_codec_status_e scenarioParseBots(scenario_json_reader_t *reader,
		scenario_document_t *document)
{
	scenario_codec_status_e status = scenarioJsonConsume(reader, '[');
	if (status != SCENARIO_CODEC_OK) return status;
	scenarioJsonSkipSpace(reader);
	if (reader->pos < reader->length && reader->json[reader->pos] == ']') {
		reader->pos++;
		return SCENARIO_CODEC_OK;
	}
	for (;;) {
		if (document->bot_count >= SCENARIO_CODEC_MAX_BOTS) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_TOO_MANY_BOTS,
				"bots array exceeds %d entries", SCENARIO_CODEC_MAX_BOTS);
		}
		status = scenarioParseBot(reader, &document->bots[document->bot_count]);
		if (status != SCENARIO_CODEC_OK) return status;
		document->bot_count++;
		scenarioJsonSkipSpace(reader);
		if (reader->pos >= reader->length) return SCENARIO_CODEC_INVALID_JSON;
		if (reader->json[reader->pos] == ']') {
			reader->pos++;
			return SCENARIO_CODEC_OK;
		}
		status = scenarioJsonConsume(reader, ',');
		if (status != SCENARIO_CODEC_OK) return status;
	}
}

static s32 scenarioWeaponKeyIndex(const char *key, const char *prefix)
{
	size_t prefix_len = strlen(prefix);
	if (strncmp(key, prefix, prefix_len) != 0) return -1;
	if (key[prefix_len] < '0' || key[prefix_len] > '5'
			|| key[prefix_len + 1] != '\0') return -1;
	return key[prefix_len] - '0';
}

static scenario_codec_status_e scenarioParseTopField(
		scenario_json_reader_t *reader, scenario_document_t *document,
		const char *key)
{
	scenario_codec_status_e status;
	s64 value;
	s32 slot;

	if (strcmp(key, "version") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_VERSION, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			INT_MIN, INT_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->version = (s32)value;
		return status;
	}
	if (strcmp(key, "name") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_NAME, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
			document->name, sizeof(document->name));
		return status;
	}
	if (strcmp(key, "arenaId") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_ARENA_ID, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
			document->arena_id, sizeof(document->arena_id));
		return status;
	}
	if (strcmp(key, "scenarioId") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_SCENARIO_ID, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
			document->scenario_id, sizeof(document->scenario_id));
		return status;
	}
	if (strcmp(key, "arena") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_LEGACY_ARENA, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			INT_MIN, INT_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->legacy_arena = (s32)value;
		return status;
	}
	if (strcmp(key, "scenario") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_LEGACY_SCENARIO, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			INT_MIN, INT_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->legacy_scenario = (s32)value;
		return status;
	}
	if (strcmp(key, "timelimit") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_TIMELIMIT, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			0, UCHAR_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->timelimit = (s32)value;
		return status;
	}
	if (strcmp(key, "scorelimit") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_SCORELIMIT, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			0, UCHAR_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->scorelimit = (s32)value;
		return status;
	}
	if (strcmp(key, "teamscorelimit") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_TEAMSCORELIMIT, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			0, USHRT_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->teamscorelimit = (u32)value;
		return status;
	}
	if (strcmp(key, "options") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_OPTIONS, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			0, 0xffffffffLL, &value);
		if (status == SCENARIO_CODEC_OK) document->options = (u32)value;
		return status;
	}
	if (strcmp(key, "weaponset") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_WEAPONSET, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			SCHAR_MIN, SCHAR_MAX, &value);
		if (status == SCENARIO_CODEC_OK) document->weaponset = (s32)value;
		return status;
	}
	if (strcmp(key, "spawnWeaponId") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_SPAWN_WEAPON_ID, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonString(reader,
			document->spawn_weapon_id, sizeof(document->spawn_weapon_id));
		return status;
	}
	if (strcmp(key, "spawnWeaponMode") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_SPAWN_WEAPON_MODE, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioJsonInteger(reader,
			0, 2, &value);
		if (status == SCENARIO_CODEC_OK) document->spawn_weapon_mode = (s32)value;
		return status;
	}
	if (strcmp(key, "bots") == 0) {
		status = scenarioJsonMark(reader, &document->present,
			SCENARIO_DOC_BOTS, key);
		if (status == SCENARIO_CODEC_OK) status = scenarioParseBots(reader, document);
		return status;
	}

	slot = scenarioWeaponKeyIndex(key, "weapon_id");
	if (slot >= 0) {
		if (document->weapon_id_present[slot]) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_DUPLICATE_FIELD,
				"duplicate field '%s'", key);
		}
		document->weapon_id_present[slot] = 1;
		return scenarioJsonString(reader, document->weapon_ids[slot],
			sizeof(document->weapon_ids[slot]));
	}
	slot = scenarioWeaponKeyIndex(key, "weapon");
	if (slot >= 0) {
		if (document->legacy_weapon_present[slot]) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_DUPLICATE_FIELD,
				"duplicate field '%s'", key);
		}
		document->legacy_weapon_present[slot] = 1;
		status = scenarioJsonInteger(reader, INT_MIN, INT_MAX, &value);
		if (status == SCENARIO_CODEC_OK) {
			document->legacy_weapons[slot] = (s32)value;
		}
		return status;
	}

	return scenarioJsonFail(reader, SCENARIO_CODEC_UNKNOWN_FIELD,
		"unknown field '%s'", key);
}

static scenario_codec_status_e scenarioValidateDocument(
		scenario_json_reader_t *reader, const scenario_document_t *document)
{
	const u32 common = SCENARIO_DOC_VERSION | SCENARIO_DOC_NAME
		| SCENARIO_DOC_TIMELIMIT | SCENARIO_DOC_SCORELIMIT
		| SCENARIO_DOC_TEAMSCORELIMIT | SCENARIO_DOC_OPTIONS
		| SCENARIO_DOC_WEAPONSET | SCENARIO_DOC_BOTS;
	s32 typed_weapons = 0;
	s32 numeric_weapons = 0;
	s32 i;

	if ((document->present & common) != common) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
			"saved Scenario is missing a required common field");
	}
	if (!document->name[0]) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
			"saved Scenario name is empty");
	}
	if (document->version < 1 || document->version > 3) {
		return scenarioJsonFail(reader, SCENARIO_CODEC_UNSUPPORTED_VERSION,
			"unsupported saved Scenario version %d", document->version);
	}

	for (i = 0; i < SCENARIO_CODEC_WEAPON_SLOTS; i++) {
		typed_weapons += document->weapon_id_present[i] ? 1 : 0;
		numeric_weapons += document->legacy_weapon_present[i] ? 1 : 0;
	}

	if (document->version == 1) {
		if ((document->present & (SCENARIO_DOC_LEGACY_ARENA
				| SCENARIO_DOC_LEGACY_SCENARIO))
				!= (SCENARIO_DOC_LEGACY_ARENA | SCENARIO_DOC_LEGACY_SCENARIO)) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v1 saved Scenario is missing numeric arena/scenario migration input");
		}
		if ((document->present & (SCENARIO_DOC_ARENA_ID
				| SCENARIO_DOC_SCENARIO_ID | SCENARIO_DOC_SPAWN_WEAPON_ID
				| SCENARIO_DOC_SPAWN_WEAPON_MODE)) != 0
				|| typed_weapons != 0 || numeric_weapons != 0) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_UNSUPPORTED_VERSION,
				"v1 saved Scenario contains fields outside the known writer shape");
		}
	} else if (document->version == 2) {
		const u32 legacy_stage_mode = SCENARIO_DOC_LEGACY_ARENA
			| SCENARIO_DOC_LEGACY_SCENARIO;
		if ((document->present & legacy_stage_mode) != legacy_stage_mode
				|| (document->present & SCENARIO_DOC_ARENA_ID) == 0
				|| !document->arena_id[0]
				|| ((document->present & SCENARIO_DOC_SCENARIO_ID) != 0
					&& !document->scenario_id[0])) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v2 saved Scenario is outside the known typed/numeric stage shape");
		}
		if (!((typed_weapons == 0 && numeric_weapons == 0)
				|| (typed_weapons == SCENARIO_CODEC_WEAPON_SLOTS
					&& numeric_weapons == SCENARIO_CODEC_WEAPON_SLOTS))) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v2 saved Scenario has a partial or unknown weapon shape");
		}
		if (((document->present & SCENARIO_DOC_SPAWN_WEAPON_MODE) != 0)
				!= ((document->present & SCENARIO_DOC_SPAWN_WEAPON_ID) != 0)) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v2 spawn weapon fields are a partial writer shape");
		}
	} else {
		const u32 typed = SCENARIO_DOC_ARENA_ID | SCENARIO_DOC_SCENARIO_ID
			| SCENARIO_DOC_SPAWN_WEAPON_ID | SCENARIO_DOC_SPAWN_WEAPON_MODE;
		if ((document->present & typed) != typed
				|| !document->arena_id[0] || !document->scenario_id[0]) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v%d saved Scenario is missing typed authority fields",
				document->version);
		}
		if (typed_weapons != SCENARIO_CODEC_WEAPON_SLOTS) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v3 saved Scenario is missing typed weapon authority");
		}
	}
	if (document->version == 3) {
		s32 numeric_companions = numeric_weapons;
		const u32 legacy_stage_mode = SCENARIO_DOC_LEGACY_ARENA
			| SCENARIO_DOC_LEGACY_SCENARIO;
		if ((document->present & SCENARIO_DOC_LEGACY_ARENA) != 0) {
			numeric_companions++;
		}
		if ((document->present & SCENARIO_DOC_LEGACY_SCENARIO) != 0) {
			numeric_companions++;
		}
		/* Shipping v3 historically wrote one exact hybrid shape: typed arena,
		 * mode, and six weapons plus all eight numeric cache companions. New
		 * writes are typed-only. Anything between those shapes is corruption. */
		if (numeric_companions != 0
				&& (numeric_companions != 8
					|| (document->present & legacy_stage_mode)
						!= legacy_stage_mode)) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"v3 legacy hybrid companion set is partial");
		}
	}

	for (i = 0; i < document->bot_count; i++) {
		const scenario_document_bot_t *bot = &document->bots[i];
		const u32 common_bot = SCENARIO_BOT_NAME | SCENARIO_BOT_DIFFICULTY;
		if ((bot->present & common_bot) != common_bot || !bot->name[0]) {
			return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
				"bot %d is missing name/difficulty", i);
		}
		if (document->version == 1) {
			const u32 numeric_bot = SCENARIO_BOT_LEGACY_BODY
				| SCENARIO_BOT_LEGACY_HEAD;
			if ((bot->present & numeric_bot) != numeric_bot) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
					"v1 bot %d is missing numeric identity migration input", i);
			}
			if ((bot->present & (SCENARIO_BOT_PROFILE_ID
					| SCENARIO_BOT_BODY_ID | SCENARIO_BOT_HEAD_ID)) != 0) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_UNSUPPORTED_VERSION,
					"v1 bot %d contains fields outside the known writer shape", i);
			}
		} else if (document->version == 2) {
			const u32 typed_identity = bot->present
				& (SCENARIO_BOT_BODY_ID | SCENARIO_BOT_HEAD_ID);
			const u32 numeric_identity = bot->present
				& (SCENARIO_BOT_LEGACY_BODY | SCENARIO_BOT_LEGACY_HEAD);
			const u32 all_typed = SCENARIO_BOT_BODY_ID | SCENARIO_BOT_HEAD_ID;
			const u32 all_numeric = SCENARIO_BOT_LEGACY_BODY
				| SCENARIO_BOT_LEGACY_HEAD;
			if ((bot->present & SCENARIO_BOT_PROFILE_ID) != 0) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_UNSUPPORTED_VERSION,
					"v2 bot %d contains a v3 profile field", i);
			}
			/* Known v2 writers used complete numeric+typed companions and then
			 * complete typed-only identity. Numeric-only remains the deliberate
			 * bounded migration input. Per-field mixtures were never a schema. */
			if (!((typed_identity == 0 && numeric_identity == all_numeric)
					|| (typed_identity == all_typed && numeric_identity == all_numeric)
					|| (typed_identity == all_typed && numeric_identity == 0))) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
					"v2 bot %d has a partial or unknown identity shape", i);
			}
			if ((typed_identity != 0)
					&& (!bot->body_id[0] || !bot->head_id[0])) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
					"v2 bot %d has an empty typed identity field", i);
			}
		} else {
			const u32 typed_bot = SCENARIO_BOT_BODY_ID | SCENARIO_BOT_HEAD_ID;
			if ((bot->present & typed_bot) != typed_bot
					|| !bot->body_id[0] || !bot->head_id[0]) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
					"v%d bot %d is missing typed body/head identity",
					document->version, i);
			}
			if (document->version == 3
					&& ((bot->present & SCENARIO_BOT_PROFILE_ID) == 0
						|| !bot->profile_id[0])) {
				return scenarioJsonFail(reader, SCENARIO_CODEC_MISSING_FIELD,
					"v3 bot %d is missing typed profile identity", i);
			}
			if (document->version == 3
					&& (bot->present & (SCENARIO_BOT_LEGACY_BODY
						| SCENARIO_BOT_LEGACY_HEAD)) != 0) {
				return scenarioJsonFail(reader,
					SCENARIO_CODEC_UNSUPPORTED_VERSION,
					"v3 bot %d must not carry numeric identity", i);
			}
		}
	}

	return SCENARIO_CODEC_OK;
}

scenario_codec_status_e scenarioDocumentParse(const char *json, size_t length,
		scenario_document_t *out, char *detail, size_t detail_size)
{
	scenario_json_reader_t reader;
	scenario_document_t candidate;
	scenario_codec_status_e status;

	if (!out) return SCENARIO_CODEC_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (detail && detail_size > 0) detail[0] = '\0';
	if (!json) return SCENARIO_CODEC_INVALID_ARGUMENT;
	if (length == 0) return SCENARIO_CODEC_EMPTY;
	if (length > SCENARIO_CODEC_MAX_BYTES) return SCENARIO_CODEC_TOO_LARGE;

	memset(&candidate, 0, sizeof(candidate));
	memset(&reader, 0, sizeof(reader));
	reader.json = json;
	reader.length = length;
	reader.detail = detail;
	reader.detail_size = detail_size;

	status = scenarioJsonConsume(&reader, '{');
	if (status != SCENARIO_CODEC_OK) return status;
	scenarioJsonSkipSpace(&reader);
	if (reader.pos < reader.length && reader.json[reader.pos] == '}') {
		reader.pos++;
	} else {
		for (;;) {
			char key[32];
			status = scenarioJsonString(&reader, key, sizeof(key));
			if (status != SCENARIO_CODEC_OK) return status;
			status = scenarioJsonConsume(&reader, ':');
			if (status != SCENARIO_CODEC_OK) return status;
			status = scenarioParseTopField(&reader, &candidate, key);
			if (status != SCENARIO_CODEC_OK) return status;
			scenarioJsonSkipSpace(&reader);
			if (reader.pos >= reader.length) {
				return scenarioJsonFail(&reader, SCENARIO_CODEC_INVALID_JSON,
					"truncated root object");
			}
			if (reader.json[reader.pos] == '}') {
				reader.pos++;
				break;
			}
			status = scenarioJsonConsume(&reader, ',');
			if (status != SCENARIO_CODEC_OK) return status;
		}
	}
	scenarioJsonSkipSpace(&reader);
	if (reader.pos != reader.length) {
		return scenarioJsonFail(&reader, SCENARIO_CODEC_INVALID_JSON,
			"trailing data at byte %u", (unsigned)reader.pos);
	}
	status = scenarioValidateDocument(&reader, &candidate);
	if (status != SCENARIO_CODEC_OK) return status;
	*out = candidate;
	return SCENARIO_CODEC_OK;
}

const char *scenarioCodecStatusString(scenario_codec_status_e status)
{
	switch (status) {
	case SCENARIO_CODEC_OK: return "ok";
	case SCENARIO_CODEC_INVALID_ARGUMENT: return "invalid_argument";
	case SCENARIO_CODEC_EMPTY: return "empty";
	case SCENARIO_CODEC_TOO_LARGE: return "too_large";
	case SCENARIO_CODEC_INVALID_JSON: return "invalid_json";
	case SCENARIO_CODEC_DUPLICATE_FIELD: return "duplicate_field";
	case SCENARIO_CODEC_UNKNOWN_FIELD: return "unknown_field";
	case SCENARIO_CODEC_WRONG_TYPE: return "wrong_type";
	case SCENARIO_CODEC_VALUE_OUT_OF_RANGE: return "value_out_of_range";
	case SCENARIO_CODEC_STRING_TOO_LONG: return "string_too_long";
	case SCENARIO_CODEC_UNSUPPORTED_VERSION: return "unsupported_version";
	case SCENARIO_CODEC_MISSING_FIELD: return "missing_field";
	case SCENARIO_CODEC_TOO_MANY_BOTS: return "too_many_bots";
	default: return "unknown";
	}
}
