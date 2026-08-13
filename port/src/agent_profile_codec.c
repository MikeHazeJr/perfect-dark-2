/**
 * agent_profile_codec.c -- pure strict Agent Profile JSON and legacy INI codec.
 */

#include <PR/ultratypes.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "agent_profile_codec.h"

typedef enum profile_token_type {
	PTOK_INVALID = 0,
	PTOK_LBRACE,
	PTOK_RBRACE,
	PTOK_LBRACKET,
	PTOK_RBRACKET,
	PTOK_COLON,
	PTOK_COMMA,
	PTOK_STRING,
	PTOK_NUMBER,
	PTOK_TRUE,
	PTOK_FALSE,
	PTOK_NULL,
	PTOK_EOF,
} profile_token_type_t;

typedef struct profile_token {
	profile_token_type_t type;
	const char *start;
	size_t length;
} profile_token_t;

typedef struct profile_parser {
	const char *cursor;
	char *error;
	size_t error_size;
} profile_parser_t;

static s32 profileFloatIsFinite(f32 value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

static s32 profileDoubleIsFinite(double value)
{
	return value >= -DBL_MAX && value <= DBL_MAX;
}

static void profileSetError(char *error, size_t error_size,
		const char *format, ...)
{
	va_list args;

	if (!error || error_size == 0 || error[0]) {
		return;
	}
	va_start(args, format);
	vsnprintf(error, error_size, format, args);
	va_end(args);
}

static void parserError(profile_parser_t *parser, const char *format, ...)
{
	va_list args;

	if (!parser || !parser->error || parser->error_size == 0
			|| parser->error[0]) {
		return;
	}
	va_start(args, format);
	vsnprintf(parser->error, parser->error_size, format, args);
	va_end(args);
}

static s32 profileIsDelimiter(char c)
{
	return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n'
		|| c == ',' || c == ']' || c == '}';
}

static profile_token_t profileNext(profile_parser_t *parser)
{
	profile_token_t token = { PTOK_INVALID, NULL, 0 };
	const char *start;
	const char *cursor;

	if (!parser || !parser->cursor) {
		return token;
	}
	cursor = parser->cursor;
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r'
			|| *cursor == '\n') {
		cursor++;
	}
	start = cursor;
	switch (*cursor) {
	case '\0': token.type = PTOK_EOF; break;
	case '{': token.type = PTOK_LBRACE; cursor++; break;
	case '}': token.type = PTOK_RBRACE; cursor++; break;
	case '[': token.type = PTOK_LBRACKET; cursor++; break;
	case ']': token.type = PTOK_RBRACKET; cursor++; break;
	case ':': token.type = PTOK_COLON; cursor++; break;
	case ',': token.type = PTOK_COMMA; cursor++; break;
	case '"':
		cursor++;
		start = cursor;
		while (*cursor && *cursor != '"') {
			if ((unsigned char)*cursor < 0x20) {
				parserError(parser, "JSON string contains a control character");
				return token;
			}
			if (*cursor == '\\') {
				/* Profile identities use a deliberately restricted ASCII domain.
				 * Reject escapes so the parser never compares encoded aliases. */
				parserError(parser, "JSON profile strings may not contain escapes");
				return token;
			}
			cursor++;
		}
		if (*cursor != '"') {
			parserError(parser, "unterminated JSON string");
			return token;
		}
		token.type = PTOK_STRING;
		token.start = start;
		token.length = (size_t)(cursor - start);
		cursor++;
		parser->cursor = cursor;
		return token;
	default:
		if (*cursor == '-' || isdigit((unsigned char)*cursor)) {
			if (*cursor == '-') cursor++;
			if (*cursor == '0') {
				cursor++;
				if (isdigit((unsigned char)*cursor)) {
					parserError(parser, "JSON number has a leading zero");
					return token;
				}
			} else if (isdigit((unsigned char)*cursor)) {
				while (isdigit((unsigned char)*cursor)) cursor++;
			} else {
				parserError(parser, "invalid JSON number");
				return token;
			}
			if (*cursor == '.') {
				cursor++;
				if (!isdigit((unsigned char)*cursor)) {
					parserError(parser, "invalid JSON fraction");
					return token;
				}
				while (isdigit((unsigned char)*cursor)) cursor++;
			}
			if (*cursor == 'e' || *cursor == 'E') {
				cursor++;
				if (*cursor == '+' || *cursor == '-') cursor++;
				if (!isdigit((unsigned char)*cursor)) {
					parserError(parser, "invalid JSON exponent");
					return token;
				}
				while (isdigit((unsigned char)*cursor)) cursor++;
			}
			if (!profileIsDelimiter(*cursor)) {
				parserError(parser, "invalid character after JSON number");
				return token;
			}
			token.type = PTOK_NUMBER;
			token.start = start;
			token.length = (size_t)(cursor - start);
			parser->cursor = cursor;
			return token;
		}
		if (strncmp(cursor, "true", 4) == 0 && profileIsDelimiter(cursor[4])) {
			token.type = PTOK_TRUE; cursor += 4;
		} else if (strncmp(cursor, "false", 5) == 0
				&& profileIsDelimiter(cursor[5])) {
			token.type = PTOK_FALSE; cursor += 5;
		} else if (strncmp(cursor, "null", 4) == 0
				&& profileIsDelimiter(cursor[4])) {
			token.type = PTOK_NULL; cursor += 4;
		} else {
			parserError(parser, "invalid JSON token");
			return token;
		}
		break;
	}

	token.start = start;
	token.length = (size_t)(cursor - start);
	parser->cursor = cursor;
	return token;
}

static s32 profileExpect(profile_parser_t *parser, profile_token_type_t type,
		const char *what)
{
	profile_token_t token = profileNext(parser);
	if (token.type != type) {
		parserError(parser, "expected %s", what);
		return -1;
	}
	return 0;
}

static s32 profileTokenEquals(const profile_token_t *token, const char *value)
{
	size_t length = strlen(value);
	return token && token->type == PTOK_STRING && token->length == length
		&& memcmp(token->start, value, length) == 0;
}

static s32 profileCopyString(const profile_token_t *token, char *out,
		size_t out_size, profile_parser_t *parser)
{
	if (!token || token->type != PTOK_STRING || !out || out_size == 0
			|| token->length >= out_size) {
		parserError(parser, "profile string is missing or too long");
		return -1;
	}
	memcpy(out, token->start, token->length);
	out[token->length] = '\0';
	return 0;
}

static s32 profileParseUnsignedToken(const profile_token_t *token, uint64_t max,
		uint64_t *out, profile_parser_t *parser)
{
	char buffer[40];
	char *end = NULL;
	unsigned long long value;

	if (!token || token->type != PTOK_NUMBER || !out || token->length == 0
			|| token->length >= sizeof(buffer)
			|| token->start[0] == '-'
			|| memchr(token->start, '.', token->length)
			|| memchr(token->start, 'e', token->length)
			|| memchr(token->start, 'E', token->length)) {
		parserError(parser, "expected an unsigned integer");
		return -1;
	}
	memcpy(buffer, token->start, token->length);
	buffer[token->length] = '\0';
	errno = 0;
	value = strtoull(buffer, &end, 10);
	if (errno == ERANGE || !end || *end || value > max) {
		parserError(parser, "unsigned integer is out of range");
		return -1;
	}
	*out = (uint64_t)value;
	return 0;
}

static s32 profileParseUnsigned(profile_parser_t *parser, uint64_t max,
		uint64_t *out)
{
	profile_token_t token = profileNext(parser);
	return profileParseUnsignedToken(&token, max, out, parser);
}

static s32 profileParseFloat(profile_parser_t *parser, f32 minimum, f32 maximum,
		f32 *out)
{
	profile_token_t token = profileNext(parser);
	char buffer[64];
	char *end = NULL;
	double value;

	if (token.type != PTOK_NUMBER || !out || token.length == 0
			|| token.length >= sizeof(buffer)) {
		parserError(parser, "expected a finite number");
		return -1;
	}
	memcpy(buffer, token.start, token.length);
	buffer[token.length] = '\0';
	errno = 0;
	value = strtod(buffer, &end);
	if (errno == ERANGE || !end || *end || !profileDoubleIsFinite(value)
			|| value < minimum || value > maximum) {
		parserError(parser, "number is out of range");
		return -1;
	}
	*out = (f32)value;
	return 0;
}

static s32 profileParseBool(profile_parser_t *parser, u8 *out)
{
	profile_token_t token = profileNext(parser);
	if (!out || (token.type != PTOK_TRUE && token.type != PTOK_FALSE)) {
		parserError(parser, "expected a JSON boolean");
		return -1;
	}
	*out = token.type == PTOK_TRUE ? 1 : 0;
	return 0;
}

static s32 profileNameIsValid(const char *name)
{
	size_t length;

	if (!name) return 0;
	length = strlen(name);
	if (length == 0 || length > AGENT_PROFILE_NAME_LENGTH_MAX
			|| name[0] == ' ' || name[length - 1] == ' ') return 0;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)name[i];
		if (!isalnum(c) && c != ' ' && c != '_' && c != '-') return 0;
	}
	return 1;
}

static s32 profileIdIsValid(const char *id, s32 allow_empty)
{
	if (!id || (!allow_empty && !id[0])) return 0;
	for (const unsigned char *cursor = (const unsigned char *)id; *cursor; cursor++) {
		if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-'
				&& *cursor != '.' && *cursor != ':' && *cursor != '/') {
			return 0;
		}
	}
	return 1;
}

static s32 profileStringListHasDuplicate(const char *values, size_t stride,
		s32 count)
{
	for (s32 i = 0; i < count; i++) {
		const char *left = values + (size_t)i * stride;
		for (s32 j = i + 1; j < count; j++) {
			const char *right = values + (size_t)j * stride;
			if (strcasecmp(left, right) == 0) return 1;
		}
	}
	return 0;
}

s32 agentProfilePreferencesValidate(
		const struct agent_profile_preferences *preferences,
		char *error, size_t error_size)
{
	if (error && error_size) error[0] = '\0';
	if (!preferences) {
		profileSetError(error, error_size, "preference candidate is null");
		return -1;
	}
	if (!profileIdIsValid(preferences->theme_id, 1)
			|| !profileIdIsValid(preferences->ui_chrome_style_id, 1)
			|| !profileIdIsValid(preferences->font_id, 1)) {
		profileSetError(error, error_size, "visual preference ID is invalid");
		return -1;
	}
	if (preferences->ui_chrome_enabled > 1
			|| preferences->ui_title_bar_style > 4
			|| preferences->scanlines > 1
			|| !profileFloatIsFinite(preferences->scanline_alpha)
			|| preferences->scanline_alpha < 0.0f
			|| preferences->scanline_alpha > 1.0f) {
		profileSetError(error, error_size, "visual preference scalar is invalid");
		return -1;
	}
	if (!profileFloatIsFinite(preferences->master_volume)
			|| !profileFloatIsFinite(preferences->music_volume)
			|| !profileFloatIsFinite(preferences->gameplay_volume)
			|| !profileFloatIsFinite(preferences->ui_volume)
			|| preferences->master_volume < 0.0f || preferences->master_volume > 1.0f
			|| preferences->music_volume < 0.0f || preferences->music_volume > 1.0f
			|| preferences->gameplay_volume < 0.0f || preferences->gameplay_volume > 1.0f
			|| preferences->ui_volume < 0.0f || preferences->ui_volume > 1.0f
			|| preferences->mod_shuffle > 1
			|| preferences->mod_playlist_count > AGENT_PROFILE_PLAYLIST_MAX) {
		profileSetError(error, error_size, "audio preference scalar is invalid");
		return -1;
	}
	for (s32 i = 0; i < preferences->mod_playlist_count; i++) {
		if (!profileIdIsValid(preferences->mod_playlist[i], 0)) {
			profileSetError(error, error_size, "playlist ID is invalid");
			return -1;
		}
	}
	if (profileStringListHasDuplicate((const char *)preferences->mod_playlist,
			AGENT_PROFILE_CATALOG_ID_MAX, preferences->mod_playlist_count)) {
		profileSetError(error, error_size, "playlist contains duplicate IDs");
		return -1;
	}
	if (preferences->center_hud > 2 || preferences->skip_intro > 1
			|| preferences->disable_mp_death_music > 1
			|| preferences->ge_muzzle_flashes > 1
			|| !profileFloatIsFinite(preferences->screen_shake_intensity)
			|| preferences->screen_shake_intensity < 0.0f
			|| preferences->screen_shake_intensity > 10.0f
			|| preferences->menu_mouse_control > 1
			|| preferences->show_dev_releases > 1
			|| preferences->enabled_mod_count > AGENT_PROFILE_ENABLED_MODS_MAX) {
		profileSetError(error, error_size, "game or update preference scalar is invalid");
		return -1;
	}
	for (s32 i = 0; i < preferences->enabled_mod_count; i++) {
		if (!profileIdIsValid(preferences->enabled_mods[i], 0)) {
			profileSetError(error, error_size, "enabled mod ID is invalid");
			return -1;
		}
	}
	if (profileStringListHasDuplicate((const char *)preferences->enabled_mods,
			AGENT_PROFILE_MOD_ID_MAX, preferences->enabled_mod_count)) {
		profileSetError(error, error_size, "enabled mod list contains duplicate IDs");
		return -1;
	}
	return 0;
}

static s32 profileDocumentValidate(
		const struct agent_profile_document *document,
		char *error, size_t error_size)
{
	if (!document || !profileNameIsValid(document->name)) {
		profileSetError(error, error_size, "Agent Profile identity is invalid");
		return -1;
	}
	if (document->autodifficulty > 7
			|| document->autostageindex > AGENT_PROFILE_STAGE_COUNT
			|| document->thumbnail > 31
			|| document->soundmode < 0 || document->soundmode > 3
			|| document->controlmode[0] > 8
			|| document->controlmode[1] > 8) {
		profileSetError(error, error_size,
			"Agent Profile game scalar is invalid");
		return -1;
	}
	for (s32 challenge = 0; challenge < AGENT_PROFILE_CHALLENGE_COUNT;
			challenge++) {
		for (s32 players = 0; players < AGENT_PROFILE_PLAYER_COUNTS;
				players++) {
			if (document->challengecompleted[challenge][players] > 1) {
				profileSetError(error, error_size,
					"Agent Profile challenge value is invalid");
				return -1;
			}
		}
	}
	return agentProfilePreferencesValidate(&document->preferences,
		error, error_size);
}

static s32 profileParseFixedU8Array(profile_parser_t *parser, u8 *values,
		s32 count)
{
	uint64_t value;
	if (profileExpect(parser, PTOK_LBRACKET, "array") != 0) return -1;
	for (s32 i = 0; i < count; i++) {
		if (profileParseUnsigned(parser, 0xff, &value) != 0) return -1;
		values[i] = (u8)value;
		if (profileExpect(parser, i + 1 < count ? PTOK_COMMA : PTOK_RBRACKET,
				i + 1 < count ? "array comma" : "array end") != 0) return -1;
	}
	return 0;
}

static s32 profileParseFixedU32Array(profile_parser_t *parser, s32 *values,
		s32 count)
{
	uint64_t value;
	if (profileExpect(parser, PTOK_LBRACKET, "array") != 0) return -1;
	for (s32 i = 0; i < count; i++) {
		if (profileParseUnsigned(parser, UINT32_MAX, &value) != 0) return -1;
		values[i] = (s32)(u32)value;
		if (profileExpect(parser, i + 1 < count ? PTOK_COMMA : PTOK_RBRACKET,
				i + 1 < count ? "array comma" : "array end") != 0) return -1;
	}
	return 0;
}

static s32 profileParseBestTimes(profile_parser_t *parser,
		u16 values[AGENT_PROFILE_STAGE_COUNT][3])
{
	uint64_t value;
	if (profileExpect(parser, PTOK_LBRACKET, "besttimes array") != 0) return -1;
	for (s32 stage = 0; stage < AGENT_PROFILE_STAGE_COUNT; stage++) {
		if (profileExpect(parser, PTOK_LBRACKET, "besttimes row") != 0) return -1;
		for (s32 difficulty = 0; difficulty < 3; difficulty++) {
			if (profileParseUnsigned(parser, UINT16_MAX, &value) != 0) return -1;
			values[stage][difficulty] = (u16)value;
			if (profileExpect(parser, difficulty < 2 ? PTOK_COMMA : PTOK_RBRACKET,
					difficulty < 2 ? "besttimes comma" : "besttimes row end") != 0) return -1;
		}
		if (profileExpect(parser,
				stage + 1 < AGENT_PROFILE_STAGE_COUNT ? PTOK_COMMA : PTOK_RBRACKET,
				stage + 1 < AGENT_PROFILE_STAGE_COUNT ? "besttimes row comma" : "besttimes end") != 0) return -1;
	}
	return 0;
}

static s32 profileParseChallenges(profile_parser_t *parser,
		u8 values[AGENT_PROFILE_CHALLENGE_COUNT][AGENT_PROFILE_PLAYER_COUNTS],
		s32 allow_legacy_integer)
{
	if (profileExpect(parser, PTOK_LBRACKET, "challenge array") != 0) return -1;
	for (s32 challenge = 0; challenge < AGENT_PROFILE_CHALLENGE_COUNT; challenge++) {
		if (profileExpect(parser, PTOK_LBRACKET, "challenge row") != 0) return -1;
		for (s32 players = 0; players < AGENT_PROFILE_PLAYER_COUNTS; players++) {
			if (allow_legacy_integer) {
				profile_token_t token = profileNext(parser);
				uint64_t value;
				if (token.type == PTOK_TRUE || token.type == PTOK_FALSE) {
					values[challenge][players] = token.type == PTOK_TRUE ? 1 : 0;
				} else if (profileParseUnsignedToken(&token, 1, &value, parser) == 0) {
					values[challenge][players] = (u8)value;
				} else {
					return -1;
				}
			} else if (profileParseBool(parser, &values[challenge][players]) != 0) {
				return -1;
			}
			if (profileExpect(parser,
					players + 1 < AGENT_PROFILE_PLAYER_COUNTS ? PTOK_COMMA : PTOK_RBRACKET,
					players + 1 < AGENT_PROFILE_PLAYER_COUNTS ? "challenge comma" : "challenge row end") != 0) return -1;
		}
		if (profileExpect(parser,
				challenge + 1 < AGENT_PROFILE_CHALLENGE_COUNT ? PTOK_COMMA : PTOK_RBRACKET,
				challenge + 1 < AGENT_PROFILE_CHALLENGE_COUNT ? "challenge row comma" : "challenge end") != 0) return -1;
	}
	return 0;
}

static s32 profileParseStringList(profile_parser_t *parser, char *values,
		size_t stride, s32 maximum, u8 *out_count, const char *label)
{
	profile_token_t token;
	s32 count = 0;

	if (profileExpect(parser, PTOK_LBRACKET, label) != 0) return -1;
	token = profileNext(parser);
	if (token.type == PTOK_RBRACKET) {
		*out_count = 0;
		return 0;
	}
	for (;;) {
		char *destination;
		if (token.type != PTOK_STRING || count >= maximum) {
			parserError(parser, "%s has an invalid length or element", label);
			return -1;
		}
		destination = values + (size_t)count * stride;
		if (profileCopyString(&token, destination, stride, parser) != 0
				|| !profileIdIsValid(destination, 0)) return -1;
		count++;
		token = profileNext(parser);
		if (token.type == PTOK_RBRACKET) break;
		if (token.type != PTOK_COMMA) {
			parserError(parser, "%s requires a comma", label);
			return -1;
		}
		token = profileNext(parser);
	}
	*out_count = (u8)count;
	return 0;
}

enum profile_preference_field {
	PREF_FIELD_THEME = 0,
	PREF_FIELD_CHROME_ID,
	PREF_FIELD_CHROME_ENABLED,
	PREF_FIELD_TITLEBAR,
	PREF_FIELD_FONT,
	PREF_FIELD_SCANLINES,
	PREF_FIELD_SCANLINE_ALPHA,
	PREF_FIELD_MASTER_VOLUME,
	PREF_FIELD_MUSIC_VOLUME,
	PREF_FIELD_GAMEPLAY_VOLUME,
	PREF_FIELD_UI_VOLUME,
	PREF_FIELD_PLAYLIST,
	PREF_FIELD_SHUFFLE,
	PREF_FIELD_CENTER_HUD,
	PREF_FIELD_SKIP_INTRO,
	PREF_FIELD_DISABLE_DEATH_MUSIC,
	PREF_FIELD_GE_MUZZLE,
	PREF_FIELD_SHAKE,
	PREF_FIELD_MENU_MOUSE,
	PREF_FIELD_DEV_RELEASES,
	PREF_FIELD_ENABLED_MODS,
	PREF_FIELD_COUNT,
};

static s32 profilePreferenceField(const profile_token_t *key)
{
	static const char *names[PREF_FIELD_COUNT] = {
		"theme_id", "ui_chrome_style_id", "ui_chrome_enabled",
		"ui_title_bar_style", "font_id", "scanlines", "scanline_alpha",
		"master_volume", "music_volume", "gameplay_volume", "ui_volume",
		"mod_playlist", "mod_shuffle", "center_hud", "skip_intro",
		"disable_mp_death_music", "ge_muzzle_flashes",
		"screen_shake_intensity", "menu_mouse_control",
		"show_dev_releases", "enabled_mods",
	};
	for (s32 i = 0; i < PREF_FIELD_COUNT; i++) {
		if (profileTokenEquals(key, names[i])) return i;
	}
	return -1;
}

static s32 profileParsePreferences(profile_parser_t *parser,
		struct agent_profile_preferences *preferences)
{
	uint64_t seen = 0;
	uint64_t value;
	profile_token_t token;

	memset(preferences, 0, sizeof(*preferences));
	if (profileExpect(parser, PTOK_LBRACE, "preferences object") != 0) return -1;
	for (;;) {
		s32 field;
		token = profileNext(parser);
		if (token.type == PTOK_RBRACE) break;
		if (token.type != PTOK_STRING
				|| profileExpect(parser, PTOK_COLON, "preference colon") != 0) return -1;
		field = profilePreferenceField(&token);
		if (field < 0) {
			parserError(parser, "unknown preference field");
			return -1;
		}
		if (seen & (UINT64_C(1) << field)) {
			parserError(parser, "duplicate preference field");
			return -1;
		}
		seen |= UINT64_C(1) << field;
		switch (field) {
		case PREF_FIELD_THEME:
			token = profileNext(parser);
			if (profileCopyString(&token, preferences->theme_id,
					sizeof(preferences->theme_id), parser) != 0) return -1;
			break;
		case PREF_FIELD_CHROME_ID:
			token = profileNext(parser);
			if (profileCopyString(&token, preferences->ui_chrome_style_id,
					sizeof(preferences->ui_chrome_style_id), parser) != 0) return -1;
			break;
		case PREF_FIELD_CHROME_ENABLED:
			if (profileParseBool(parser, &preferences->ui_chrome_enabled) != 0) return -1;
			break;
		case PREF_FIELD_TITLEBAR:
			if (profileParseUnsigned(parser, 4, &value) != 0) return -1;
			preferences->ui_title_bar_style = (u8)value;
			break;
		case PREF_FIELD_FONT:
			token = profileNext(parser);
			if (profileCopyString(&token, preferences->font_id,
					sizeof(preferences->font_id), parser) != 0) return -1;
			break;
		case PREF_FIELD_SCANLINES:
			if (profileParseBool(parser, &preferences->scanlines) != 0) return -1;
			break;
		case PREF_FIELD_SCANLINE_ALPHA:
			if (profileParseFloat(parser, 0.0f, 1.0f,
					&preferences->scanline_alpha) != 0) return -1;
			break;
		case PREF_FIELD_MASTER_VOLUME:
			if (profileParseFloat(parser, 0.0f, 1.0f,
					&preferences->master_volume) != 0) return -1;
			break;
		case PREF_FIELD_MUSIC_VOLUME:
			if (profileParseFloat(parser, 0.0f, 1.0f,
					&preferences->music_volume) != 0) return -1;
			break;
		case PREF_FIELD_GAMEPLAY_VOLUME:
			if (profileParseFloat(parser, 0.0f, 1.0f,
					&preferences->gameplay_volume) != 0) return -1;
			break;
		case PREF_FIELD_UI_VOLUME:
			if (profileParseFloat(parser, 0.0f, 1.0f,
					&preferences->ui_volume) != 0) return -1;
			break;
		case PREF_FIELD_PLAYLIST:
			if (profileParseStringList(parser,
					(char *)preferences->mod_playlist,
					AGENT_PROFILE_CATALOG_ID_MAX,
					AGENT_PROFILE_PLAYLIST_MAX,
					&preferences->mod_playlist_count, "mod playlist") != 0) return -1;
			break;
		case PREF_FIELD_SHUFFLE:
			if (profileParseBool(parser, &preferences->mod_shuffle) != 0) return -1;
			break;
		case PREF_FIELD_CENTER_HUD:
			if (profileParseUnsigned(parser, 2, &value) != 0) return -1;
			preferences->center_hud = (u8)value;
			break;
		case PREF_FIELD_SKIP_INTRO:
			if (profileParseBool(parser, &preferences->skip_intro) != 0) return -1;
			break;
		case PREF_FIELD_DISABLE_DEATH_MUSIC:
			if (profileParseBool(parser,
					&preferences->disable_mp_death_music) != 0) return -1;
			break;
		case PREF_FIELD_GE_MUZZLE:
			if (profileParseBool(parser, &preferences->ge_muzzle_flashes) != 0) return -1;
			break;
		case PREF_FIELD_SHAKE:
			if (profileParseFloat(parser, 0.0f, 10.0f,
					&preferences->screen_shake_intensity) != 0) return -1;
			break;
		case PREF_FIELD_MENU_MOUSE:
			if (profileParseBool(parser, &preferences->menu_mouse_control) != 0) return -1;
			break;
		case PREF_FIELD_DEV_RELEASES:
			if (profileParseBool(parser, &preferences->show_dev_releases) != 0) return -1;
			break;
		case PREF_FIELD_ENABLED_MODS:
			if (profileParseStringList(parser,
					(char *)preferences->enabled_mods,
					AGENT_PROFILE_MOD_ID_MAX,
					AGENT_PROFILE_ENABLED_MODS_MAX,
					&preferences->enabled_mod_count, "enabled mods") != 0) return -1;
			break;
		default: return -1;
		}
		token = profileNext(parser);
		if (token.type == PTOK_RBRACE) break;
		if (token.type != PTOK_COMMA) {
			parserError(parser, "preferences object requires a comma");
			return -1;
		}
	}
	if (seen != ((UINT64_C(1) << PREF_FIELD_COUNT) - 1)) {
		parserError(parser, "preferences object is incomplete");
		return -1;
	}
	return agentProfilePreferencesValidate(preferences,
		parser->error, parser->error_size);
}

enum profile_root_field {
	ROOT_FIELD_VERSION = 0,
	ROOT_FIELD_NAME,
	ROOT_FIELD_TOTALTIME,
	ROOT_FIELD_AUTODIFFICULTY,
	ROOT_FIELD_AUTOSTAGE,
	ROOT_FIELD_THUMBNAIL,
	ROOT_FIELD_BESTTIMES,
	ROOT_FIELD_COOP,
	ROOT_FIELD_FIRING_RANGE,
	ROOT_FIELD_WEAPONS,
	ROOT_FIELD_FLAGS,
	ROOT_FIELD_UNK1E,
	ROOT_FIELD_SFX,
	ROOT_FIELD_MUSIC,
	ROOT_FIELD_SOUNDMODE,
	ROOT_FIELD_CONTROLS,
	ROOT_FIELD_CHALLENGES,
	ROOT_FIELD_PREFERENCES,
	ROOT_FIELD_COUNT,
};

static s32 profileRootField(const profile_token_t *key)
{
	static const char *names[ROOT_FIELD_COUNT] = {
		"version", "name", "totaltime", "autodifficulty",
		"autostageindex", "thumbnail", "besttimes", "coopcompletions",
		"firingrangescores", "weaponsfound", "flags", "unk1e",
		"sfxvolume", "musicvolume", "soundmode", "controlmode",
		"challengecompleted", "preferences",
	};
	for (s32 i = 0; i < ROOT_FIELD_COUNT; i++) {
		if (profileTokenEquals(key, names[i])) return i;
	}
	return -1;
}

s32 agentProfileParseJson(const char *json, const char *expected_name,
		const struct agent_profile_document *legacy_defaults,
		struct agent_profile_document *out,
		enum agent_profile_source_shape *out_shape,
		char *error, size_t error_size)
{
	profile_parser_t parser;
	profile_token_t token;
	struct agent_profile_document candidate;
	uint64_t seen = 0;
	uint64_t value;
	u32 version = 0;
	const uint64_t legacy_core_mask = (UINT64_C(1) << (ROOT_FIELD_WEAPONS + 1)) - 1;
	const uint64_t legacy_transitional_mask = (UINT64_C(1) << (ROOT_FIELD_CHALLENGES + 1)) - 1;
	const uint64_t current_mask = (UINT64_C(1) << ROOT_FIELD_COUNT) - 1;

	if (error && error_size) error[0] = '\0';
	if (!json || !legacy_defaults || !out) {
		profileSetError(error, error_size, "profile parse arguments are incomplete");
		return -1;
	}
	candidate = *legacy_defaults;
	memset(&parser, 0, sizeof(parser));
	parser.cursor = json;
	parser.error = error;
	parser.error_size = error_size;
	if (profileExpect(&parser, PTOK_LBRACE, "profile object") != 0) return -1;
	for (;;) {
		s32 field;
		token = profileNext(&parser);
		if (token.type == PTOK_RBRACE) break;
		if (token.type != PTOK_STRING
				|| profileExpect(&parser, PTOK_COLON, "profile field colon") != 0) return -1;
		field = profileRootField(&token);
		if (field < 0) {
			parserError(&parser, "unknown Agent Profile field");
			return -1;
		}
		if (seen & (UINT64_C(1) << field)) {
			parserError(&parser, "duplicate Agent Profile field");
			return -1;
		}
		if (seen == 0 && field != ROOT_FIELD_VERSION) {
			parserError(&parser, "Agent Profile version must be the first field");
			return -1;
		}
		seen |= UINT64_C(1) << field;
		switch (field) {
		case ROOT_FIELD_VERSION:
			if (profileParseUnsigned(&parser, INT32_MAX, &value) != 0) return -1;
			version = (u32)value;
			break;
		case ROOT_FIELD_NAME:
			token = profileNext(&parser);
			if (profileCopyString(&token, candidate.name,
					sizeof(candidate.name), &parser) != 0
					|| !profileNameIsValid(candidate.name)) {
				parserError(&parser, "Agent Profile name is invalid");
				return -1;
			}
			break;
		case ROOT_FIELD_TOTALTIME:
			if (profileParseUnsigned(&parser, UINT32_MAX, &value) != 0) return -1;
			candidate.totaltime = (u32)value;
			break;
		case ROOT_FIELD_AUTODIFFICULTY:
			if (profileParseUnsigned(&parser, 7, &value) != 0) return -1;
			candidate.autodifficulty = (u8)value;
			break;
		case ROOT_FIELD_AUTOSTAGE:
			if (profileParseUnsigned(&parser, AGENT_PROFILE_STAGE_COUNT, &value) != 0) return -1;
			candidate.autostageindex = (u8)value;
			break;
		case ROOT_FIELD_THUMBNAIL:
			if (profileParseUnsigned(&parser, 31, &value) != 0) return -1;
			candidate.thumbnail = (u8)value;
			break;
		case ROOT_FIELD_BESTTIMES:
			if (profileParseBestTimes(&parser, candidate.besttimes) != 0) return -1;
			break;
		case ROOT_FIELD_COOP:
			if (profileParseFixedU32Array(&parser, candidate.coopcompletions, 3) != 0) return -1;
			break;
		case ROOT_FIELD_FIRING_RANGE:
			if (profileParseFixedU8Array(&parser, candidate.firingrangescores, 9) != 0) return -1;
			break;
		case ROOT_FIELD_WEAPONS:
			if (profileParseFixedU8Array(&parser, candidate.weaponsfound, 6) != 0) return -1;
			break;
		case ROOT_FIELD_FLAGS:
			if (profileParseFixedU8Array(&parser, candidate.flags, 10) != 0) return -1;
			break;
		case ROOT_FIELD_UNK1E:
			if (profileParseUnsigned(&parser, UINT16_MAX, &value) != 0) return -1;
			candidate.unk1e = (u16)value;
			break;
		case ROOT_FIELD_SFX:
			if (profileParseUnsigned(&parser, UINT16_MAX, &value) != 0) return -1;
			candidate.sfxvolume = (u16)value;
			break;
		case ROOT_FIELD_MUSIC:
			if (profileParseUnsigned(&parser, UINT16_MAX, &value) != 0) return -1;
			candidate.musicvolume = (u16)value;
			break;
		case ROOT_FIELD_SOUNDMODE:
			if (profileParseUnsigned(&parser, 3, &value) != 0) return -1;
			candidate.soundmode = (s32)value;
			break;
		case ROOT_FIELD_CONTROLS:
			if (profileParseFixedU8Array(&parser, candidate.controlmode, 2) != 0
					|| candidate.controlmode[0] > 8 || candidate.controlmode[1] > 8) {
				parserError(&parser, "control mode is out of range");
				return -1;
			}
			break;
		case ROOT_FIELD_CHALLENGES:
			if (profileParseChallenges(&parser, candidate.challengecompleted,
					version == AGENT_PROFILE_LEGACY_VERSION) != 0) return -1;
			break;
		case ROOT_FIELD_PREFERENCES:
			if (profileParsePreferences(&parser, &candidate.preferences) != 0) return -1;
			break;
		default: return -1;
		}
		token = profileNext(&parser);
		if (token.type == PTOK_RBRACE) break;
		if (token.type != PTOK_COMMA) {
			parserError(&parser, "Agent Profile object requires a comma");
			return -1;
		}
	}
	if (profileNext(&parser).type != PTOK_EOF) {
		parserError(&parser, "trailing data follows Agent Profile JSON");
		return -1;
	}
	if (version == AGENT_PROFILE_VERSION && seen == current_mask) {
		if (out_shape) *out_shape = AGENT_PROFILE_SOURCE_CURRENT;
	} else if (version == AGENT_PROFILE_LEGACY_VERSION
			&& seen == legacy_core_mask) {
		if (out_shape) *out_shape = AGENT_PROFILE_SOURCE_LEGACY_CORE_V2;
	} else if (version == AGENT_PROFILE_LEGACY_VERSION
			&& seen == legacy_transitional_mask) {
		if (out_shape) *out_shape = AGENT_PROFILE_SOURCE_LEGACY_TRANSITIONAL_V2;
	} else {
		profileSetError(error, error_size,
			"Agent Profile is not a complete current or exact known v2 document");
		return -1;
	}
	if (expected_name && strcmp(candidate.name, expected_name) != 0) {
		profileSetError(error, error_size, "Agent Profile identity does not match its request");
		return -1;
	}
	if (profileDocumentValidate(&candidate, error, error_size) != 0) return -1;
	*out = candidate;
	return 0;
}

static void profileWriteString(FILE *stream, const char *value)
{
	fputc('"', stream);
	for (const unsigned char *cursor = (const unsigned char *)(value ? value : "");
			*cursor; cursor++) {
		switch (*cursor) {
		case '"': fputs("\\\"", stream); break;
		case '\\': fputs("\\\\", stream); break;
		case '\n': fputs("\\n", stream); break;
		case '\r': fputs("\\r", stream); break;
		case '\t': fputs("\\t", stream); break;
		default: fputc(*cursor, stream); break;
		}
	}
	fputc('"', stream);
}

static void profileWriteStringList(FILE *stream, const char *values,
		size_t stride, s32 count)
{
	fputc('[', stream);
	for (s32 i = 0; i < count; i++) {
		if (i) fputs(", ", stream);
		profileWriteString(stream, values + (size_t)i * stride);
	}
	fputc(']', stream);
}

s32 agentProfileWriteJson(FILE *stream,
		const struct agent_profile_document *document)
{
	char error[160];
	if (!stream || !document
			|| profileDocumentValidate(document, error, sizeof(error)) != 0) {
		return -1;
	}
	fprintf(stream, "{\n  \"version\": %d,\n  \"name\": ", AGENT_PROFILE_VERSION);
	profileWriteString(stream, document->name);
	fprintf(stream,
		",\n  \"totaltime\": %u,\n  \"autodifficulty\": %u,\n"
		"  \"autostageindex\": %u,\n  \"thumbnail\": %u,\n  \"besttimes\": [\n",
		document->totaltime, document->autodifficulty,
		document->autostageindex, document->thumbnail);
	for (s32 stage = 0; stage < AGENT_PROFILE_STAGE_COUNT; stage++) {
		fprintf(stream, "    [%u, %u, %u]%s\n",
			document->besttimes[stage][0], document->besttimes[stage][1],
			document->besttimes[stage][2],
			stage + 1 < AGENT_PROFILE_STAGE_COUNT ? "," : "");
	}
	fprintf(stream, "  ],\n  \"coopcompletions\": [%u, %u, %u],\n",
		(u32)document->coopcompletions[0], (u32)document->coopcompletions[1],
		(u32)document->coopcompletions[2]);
	fputs("  \"firingrangescores\": [", stream);
	for (s32 i = 0; i < 9; i++) fprintf(stream, "%u%s", document->firingrangescores[i], i < 8 ? ", " : "");
	fputs("],\n  \"weaponsfound\": [", stream);
	for (s32 i = 0; i < 6; i++) fprintf(stream, "%u%s", document->weaponsfound[i], i < 5 ? ", " : "");
	fputs("],\n  \"flags\": [", stream);
	for (s32 i = 0; i < 10; i++) fprintf(stream, "%u%s", document->flags[i], i < 9 ? ", " : "");
	fprintf(stream,
		"],\n  \"unk1e\": %u,\n  \"sfxvolume\": %u,\n"
		"  \"musicvolume\": %u,\n  \"soundmode\": %d,\n"
		"  \"controlmode\": [%u, %u],\n  \"challengecompleted\": [\n",
		document->unk1e, document->sfxvolume, document->musicvolume,
		document->soundmode, document->controlmode[0], document->controlmode[1]);
	for (s32 challenge = 0; challenge < AGENT_PROFILE_CHALLENGE_COUNT; challenge++) {
		fputs("    [", stream);
		for (s32 players = 0; players < AGENT_PROFILE_PLAYER_COUNTS; players++) {
			fputs(document->challengecompleted[challenge][players] ? "true" : "false", stream);
			if (players + 1 < AGENT_PROFILE_PLAYER_COUNTS) fputs(", ", stream);
		}
		fprintf(stream, "]%s\n",
			challenge + 1 < AGENT_PROFILE_CHALLENGE_COUNT ? "," : "");
	}
	fputs("  ],\n  \"preferences\": {\n    \"theme_id\": ", stream);
	profileWriteString(stream, document->preferences.theme_id);
	fputs(",\n    \"ui_chrome_style_id\": ", stream);
	profileWriteString(stream, document->preferences.ui_chrome_style_id);
	fprintf(stream,
		",\n    \"ui_chrome_enabled\": %s,\n"
		"    \"ui_title_bar_style\": %u,\n    \"font_id\": ",
		document->preferences.ui_chrome_enabled ? "true" : "false",
		document->preferences.ui_title_bar_style);
	profileWriteString(stream, document->preferences.font_id);
	fprintf(stream,
		",\n    \"scanlines\": %s,\n    \"scanline_alpha\": %.9g,\n"
		"    \"master_volume\": %.9g,\n    \"music_volume\": %.9g,\n"
		"    \"gameplay_volume\": %.9g,\n    \"ui_volume\": %.9g,\n"
		"    \"mod_playlist\": ",
		document->preferences.scanlines ? "true" : "false",
		(double)document->preferences.scanline_alpha,
		(double)document->preferences.master_volume,
		(double)document->preferences.music_volume,
		(double)document->preferences.gameplay_volume,
		(double)document->preferences.ui_volume);
	profileWriteStringList(stream, (const char *)document->preferences.mod_playlist,
		AGENT_PROFILE_CATALOG_ID_MAX, document->preferences.mod_playlist_count);
	fprintf(stream,
		",\n    \"mod_shuffle\": %s,\n    \"center_hud\": %u,\n"
		"    \"skip_intro\": %s,\n    \"disable_mp_death_music\": %s,\n"
		"    \"ge_muzzle_flashes\": %s,\n"
		"    \"screen_shake_intensity\": %.9g,\n"
		"    \"menu_mouse_control\": %s,\n"
		"    \"show_dev_releases\": %s,\n    \"enabled_mods\": ",
		document->preferences.mod_shuffle ? "true" : "false",
		document->preferences.center_hud,
		document->preferences.skip_intro ? "true" : "false",
		document->preferences.disable_mp_death_music ? "true" : "false",
		document->preferences.ge_muzzle_flashes ? "true" : "false",
		(double)document->preferences.screen_shake_intensity,
		document->preferences.menu_mouse_control ? "true" : "false",
		document->preferences.show_dev_releases ? "true" : "false");
	profileWriteStringList(stream, (const char *)document->preferences.enabled_mods,
		AGENT_PROFILE_MOD_ID_MAX, document->preferences.enabled_mod_count);
	fputs("\n  }\n}\n", stream);
	return ferror(stream) ? -1 : 0;
}

static char *profileTrim(char *value)
{
	char *end;
	while (*value && isspace((unsigned char)*value)) value++;
	if (!*value) return value;
	end = value + strlen(value) - 1;
	while (end >= value && isspace((unsigned char)*end)) *end-- = '\0';
	return value;
}

static s32 profileIniParseUnsigned(const char *text, u32 maximum, u32 *out)
{
	char *end = NULL;
	unsigned long value;
	if (!text || !text[0] || text[0] == '-') return -1;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno == ERANGE || !end || *end || value > maximum) return -1;
	*out = (u32)value;
	return 0;
}

static s32 profileIniParseFloat(const char *text, f32 minimum, f32 maximum,
		f32 *out)
{
	char *end = NULL;
	double value;
	if (!text || !text[0]) return -1;
	errno = 0;
	value = strtod(text, &end);
	if (errno == ERANGE || !end || *end || !profileDoubleIsFinite(value)
			|| value < minimum || value > maximum) return -1;
	*out = (f32)value;
	return 0;
}

static s32 profileIniParseList(char *text, char *values, size_t stride,
		s32 maximum, u8 *out_count, char separator)
{
	s32 count = 0;
	char *cursor = text;
	while (cursor && *cursor) {
		char *next = strchr(cursor, separator);
		char *item;
		if (next) *next = '\0';
		item = profileTrim(cursor);
		if (!item[0] || count >= maximum || strlen(item) >= stride
				|| !profileIdIsValid(item, 0)) return -1;
		strcpy(values + (size_t)count * stride, item);
		count++;
		cursor = next ? next + 1 : NULL;
	}
	*out_count = (u8)count;
	return profileStringListHasDuplicate(values, stride, count) ? -1 : 0;
}

enum profile_ini_field {
	INI_THEME = 0, INI_CHROME_ID, INI_CHROME_ENABLED, INI_TITLEBAR,
	INI_FONT, INI_SCANLINES, INI_SCANLINE_ALPHA, INI_MASTER, INI_MUSIC,
	INI_GAMEPLAY, INI_UI, INI_PLAYLIST, INI_SHUFFLE, INI_LEGACY_TRACK,
	INI_CENTER_HUD, INI_SKIP_INTRO, INI_DISABLE_DEATH, INI_GE_MUZZLE,
	INI_SHAKE, INI_MENU_MOUSE, INI_DEV_RELEASES, INI_ENABLED_MODS,
	INI_FIELD_COUNT,
};

static s32 profileIniField(const char *section, const char *key)
{
	if (!strcasecmp(section, "Theme") && !strcasecmp(key, "ActiveId")) return INI_THEME;
	if (!strcasecmp(section, "Video")) {
		if (!strcasecmp(key, "UiChromeStyleId")) return INI_CHROME_ID;
		if (!strcasecmp(key, "UiChromeEnabled")) return INI_CHROME_ENABLED;
		if (!strcasecmp(key, "UiTitleBarStyle")) return INI_TITLEBAR;
		if (!strcasecmp(key, "FontId")) return INI_FONT;
		if (!strcasecmp(key, "Scanlines")) return INI_SCANLINES;
		if (!strcasecmp(key, "ScanlineAlpha")) return INI_SCANLINE_ALPHA;
	}
	if (!strcasecmp(section, "Audio")) {
		if (!strcasecmp(key, "MasterVolume")) return INI_MASTER;
		if (!strcasecmp(key, "MusicVolume")) return INI_MUSIC;
		if (!strcasecmp(key, "GameplayVolume")) return INI_GAMEPLAY;
		if (!strcasecmp(key, "UIVolume")) return INI_UI;
		if (!strcasecmp(key, "ModPlaylist")) return INI_PLAYLIST;
		if (!strcasecmp(key, "ModShuffle")) return INI_SHUFFLE;
		if (!strcasecmp(key, "ModTrackId")) return INI_LEGACY_TRACK;
	}
	if (!strcasecmp(section, "Game")) {
		if (!strcasecmp(key, "CenterHUD")) return INI_CENTER_HUD;
		if (!strcasecmp(key, "SkipIntro")) return INI_SKIP_INTRO;
		if (!strcasecmp(key, "DisableMpDeathMusic")) return INI_DISABLE_DEATH;
		if (!strcasecmp(key, "GEMuzzleFlashes")) return INI_GE_MUZZLE;
		if (!strcasecmp(key, "ScreenShakeIntensity")) return INI_SHAKE;
		if (!strcasecmp(key, "MenuMouseControl")) return INI_MENU_MOUSE;
	}
	if (!strcasecmp(section, "Updates")
			&& !strcasecmp(key, "ShowDevReleases")) return INI_DEV_RELEASES;
	if (!strcasecmp(section, "Mods") && !strcasecmp(key, "Enabled")) return INI_ENABLED_MODS;
	return -1;
}

s32 agentProfileParseLegacyPreferencesIni(const char *ini,
		const struct agent_profile_preferences *defaults,
		struct agent_profile_preferences *out,
		char *error, size_t error_size)
{
	struct agent_profile_preferences candidate;
	char *buffer;
	char *cursor;
	char section[64] = "";
	char legacy_track[AGENT_PROFILE_CATALOG_ID_MAX] = "";
	uint64_t seen = 0;

	if (error && error_size) error[0] = '\0';
	if (!ini || !defaults || !out) {
		profileSetError(error, error_size, "legacy INI parse arguments are incomplete");
		return -1;
	}
	candidate = *defaults;
	buffer = (char *)malloc(strlen(ini) + 1);
	if (!buffer) {
		profileSetError(error, error_size, "out of memory parsing legacy INI");
		return -1;
	}
	strcpy(buffer, ini);
	cursor = buffer;
	while (cursor) {
		char *next = strchr(cursor, '\n');
		char *line;
		char *equals;
		s32 field;
		u32 integer;
		if (next) *next = '\0';
		line = profileTrim(cursor);
		if (line[0] && line[0] != '#' && line[0] != ';') {
			size_t length = strlen(line);
			if (line[0] == '[') {
				if (length < 3 || line[length - 1] != ']'
						|| length - 2 >= sizeof(section)) goto malformed;
				memcpy(section, line + 1, length - 2);
				section[length - 2] = '\0';
			} else {
				equals = strchr(line, '=');
				if (!equals || !section[0]) goto malformed;
				*equals = '\0';
				char *key = profileTrim(line);
				char *value = profileTrim(equals + 1);
				if (!key[0]) goto malformed;
				field = profileIniField(section, key);
				if (field >= 0) {
					if (seen & (UINT64_C(1) << field)) goto duplicate;
					seen |= UINT64_C(1) << field;
					switch (field) {
					case INI_THEME:
						if (strlen(value) >= sizeof(candidate.theme_id)
								|| !profileIdIsValid(value, 1)) goto invalid_value;
						strcpy(candidate.theme_id, value); break;
					case INI_CHROME_ID:
						if (strlen(value) >= sizeof(candidate.ui_chrome_style_id)
								|| !profileIdIsValid(value, 1)) goto invalid_value;
						strcpy(candidate.ui_chrome_style_id, value); break;
					case INI_FONT:
						if (strlen(value) >= sizeof(candidate.font_id)
								|| !profileIdIsValid(value, 1)) goto invalid_value;
						strcpy(candidate.font_id, value); break;
					case INI_CHROME_ENABLED:
					case INI_SCANLINES:
					case INI_SHUFFLE:
					case INI_SKIP_INTRO:
					case INI_DISABLE_DEATH:
					case INI_GE_MUZZLE:
					case INI_MENU_MOUSE:
					case INI_DEV_RELEASES:
						if (profileIniParseUnsigned(value, 1, &integer) != 0) goto invalid_value;
						if (field == INI_CHROME_ENABLED) candidate.ui_chrome_enabled = (u8)integer;
						else if (field == INI_SCANLINES) candidate.scanlines = (u8)integer;
						else if (field == INI_SHUFFLE) candidate.mod_shuffle = (u8)integer;
						else if (field == INI_SKIP_INTRO) candidate.skip_intro = (u8)integer;
						else if (field == INI_DISABLE_DEATH) candidate.disable_mp_death_music = (u8)integer;
						else if (field == INI_GE_MUZZLE) candidate.ge_muzzle_flashes = (u8)integer;
						else if (field == INI_MENU_MOUSE) candidate.menu_mouse_control = (u8)integer;
						else candidate.show_dev_releases = (u8)integer;
						break;
					case INI_TITLEBAR:
						if (profileIniParseUnsigned(value, 4, &integer) != 0) goto invalid_value;
						candidate.ui_title_bar_style = (u8)integer; break;
					case INI_CENTER_HUD:
						if (profileIniParseUnsigned(value, 2, &integer) != 0) goto invalid_value;
						candidate.center_hud = (u8)integer; break;
					case INI_SCANLINE_ALPHA:
						if (profileIniParseFloat(value, 0.0f, 1.0f,
								&candidate.scanline_alpha) != 0) goto invalid_value;
						break;
					case INI_MASTER:
						if (profileIniParseFloat(value, 0.0f, 1.0f,
								&candidate.master_volume) != 0) goto invalid_value;
						break;
					case INI_MUSIC:
						if (profileIniParseFloat(value, 0.0f, 1.0f,
								&candidate.music_volume) != 0) goto invalid_value;
						break;
					case INI_GAMEPLAY:
						if (profileIniParseFloat(value, 0.0f, 1.0f,
								&candidate.gameplay_volume) != 0) goto invalid_value;
						break;
					case INI_UI:
						if (profileIniParseFloat(value, 0.0f, 1.0f,
								&candidate.ui_volume) != 0) goto invalid_value;
						break;
					case INI_SHAKE:
						if (profileIniParseFloat(value, 0.0f, 10.0f,
								&candidate.screen_shake_intensity) != 0) goto invalid_value;
						break;
					case INI_PLAYLIST:
						memset(candidate.mod_playlist, 0, sizeof(candidate.mod_playlist));
						candidate.mod_playlist_count = 0;
						if (value[0] && profileIniParseList(value,
								(char *)candidate.mod_playlist,
								AGENT_PROFILE_CATALOG_ID_MAX,
								AGENT_PROFILE_PLAYLIST_MAX,
								&candidate.mod_playlist_count, ';') != 0) goto invalid_value;
						break;
					case INI_ENABLED_MODS:
						memset(candidate.enabled_mods, 0, sizeof(candidate.enabled_mods));
						candidate.enabled_mod_count = 0;
						if (value[0] && profileIniParseList(value,
								(char *)candidate.enabled_mods,
								AGENT_PROFILE_MOD_ID_MAX,
								AGENT_PROFILE_ENABLED_MODS_MAX,
								&candidate.enabled_mod_count, ',') != 0) goto invalid_value;
						break;
					case INI_LEGACY_TRACK:
						if (strlen(value) >= sizeof(legacy_track)
								|| !profileIdIsValid(value, 1)) goto invalid_value;
						strcpy(legacy_track, value); break;
					default: goto invalid_value;
					}
				}
			}
		}
		cursor = next ? next + 1 : NULL;
	}
	if (!(seen & (UINT64_C(1) << INI_PLAYLIST)) && legacy_track[0]) {
		memset(candidate.mod_playlist, 0, sizeof(candidate.mod_playlist));
		strcpy(candidate.mod_playlist[0], legacy_track);
		candidate.mod_playlist_count = 1;
	}
	free(buffer);
	if (agentProfilePreferencesValidate(&candidate, error, error_size) != 0) return -1;
	*out = candidate;
	return 0;

malformed:
	profileSetError(error, error_size, "legacy INI contains a malformed line");
	free(buffer); return -1;
duplicate:
	profileSetError(error, error_size, "legacy INI repeats a recognized key");
	free(buffer); return -1;
invalid_value:
	profileSetError(error, error_size, "legacy INI contains an invalid recognized value");
	free(buffer); return -1;
}
