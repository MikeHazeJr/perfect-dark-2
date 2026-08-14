#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "smoke_fixture_schema.h"

#define SMOKE_JSON_MAX_DEPTH 128u

typedef struct smoke_json_cursor {
	const char *at;
	unsigned depth;
} smoke_json_cursor_t;

typedef struct smoke_fixture_field_entry {
	const char *name;
	smoke_fixture_field_mask_t field;
} smoke_fixture_field_entry_t;

static const smoke_fixture_field_entry_t k_event_fields[] = {
	{ "at_ms", SMOKE_FIXTURE_FIELD_AT_MS },
	{ "type", SMOKE_FIXTURE_FIELD_TYPE },
	{ "key", SMOKE_FIXTURE_FIELD_KEY },
	{ "scancode", SMOKE_FIXTURE_FIELD_SCANCODE },
	{ "action", SMOKE_FIXTURE_FIELD_ACTION },
	{ "name", SMOKE_FIXTURE_FIELD_NAME },
	{ "condition", SMOKE_FIXTURE_FIELD_CONDITION },
	{ "timeout_ms", SMOKE_FIXTURE_FIELD_TIMEOUT_MS },
	{ "stable_ms", SMOKE_FIXTURE_FIELD_STABLE_MS },
	{ "assist_action", SMOKE_FIXTURE_FIELD_ASSIST_ACTION },
	{ "assist_condition", SMOKE_FIXTURE_FIELD_ASSIST_CONDITION },
	{ "assist_hold_ms", SMOKE_FIXTURE_FIELD_ASSIST_HOLD_MS },
	{ "path", SMOKE_FIXTURE_FIELD_PATH },
	{ "x", SMOKE_FIXTURE_FIELD_X },
	{ "y", SMOKE_FIXTURE_FIELD_Y },
	{ "button", SMOKE_FIXTURE_FIELD_BUTTON },
	{ "wheel_x", SMOKE_FIXTURE_FIELD_WHEEL_X },
	{ "wheel_y", SMOKE_FIXTURE_FIELD_WHEEL_Y },
	{ "comment", SMOKE_FIXTURE_FIELD_COMMENT },
	{ "client_id", SMOKE_FIXTURE_FIELD_CLIENT_ID },
};

static void smokeJsonSkipWhitespace(smoke_json_cursor_t *cursor)
{
	for (;;) {
		while (*cursor->at && isspace((unsigned char)*cursor->at)) {
			cursor->at++;
		}
		if (cursor->at[0] != '/' || cursor->at[1] != '/') {
			return;
		}
		cursor->at += 2;
		while (*cursor->at && *cursor->at != '\n') {
			cursor->at++;
		}
	}
}

static int smokeJsonParseString(smoke_json_cursor_t *cursor)
{
	if (*cursor->at != '"') {
		return 0;
	}
	cursor->at++;
	while (*cursor->at) {
		unsigned char ch = (unsigned char)*cursor->at++;
		if (ch == '"') {
			return 1;
		}
		if (ch < 0x20) {
			return 0;
		}
		if (ch != '\\') {
			continue;
		}

		ch = (unsigned char)*cursor->at++;
		if (!ch) {
			return 0;
		}
		if (ch == 'u') {
			for (int i = 0; i < 4; i++) {
				if (!*cursor->at
						|| !isxdigit((unsigned char)*cursor->at)) {
					return 0;
				}
				cursor->at++;
			}
		} else if (!strchr("\"\\/bfnrt", ch)) {
			return 0;
		}
	}
	return 0;
}

static int smokeJsonParseNumber(smoke_json_cursor_t *cursor)
{
	const char *start = cursor->at;

	if (*cursor->at == '-') {
		cursor->at++;
	}
	if (cursor->at[0] == '0'
			&& (cursor->at[1] == 'x' || cursor->at[1] == 'X')) {
		/* Preserve the harness's existing authoring extension. */
		if (start != cursor->at) {
			return 0;
		}
		cursor->at += 2;
		if (!isxdigit((unsigned char)*cursor->at)) {
			return 0;
		}
		while (isxdigit((unsigned char)*cursor->at)) {
			cursor->at++;
		}
		return 1;
	}

	if (*cursor->at == '0') {
		cursor->at++;
		if (isdigit((unsigned char)*cursor->at)) {
			return 0;
		}
	} else {
		if (!isdigit((unsigned char)*cursor->at)) {
			return 0;
		}
		while (isdigit((unsigned char)*cursor->at)) {
			cursor->at++;
		}
	}
	if (*cursor->at == '.') {
		cursor->at++;
		if (!isdigit((unsigned char)*cursor->at)) {
			return 0;
		}
		while (isdigit((unsigned char)*cursor->at)) {
			cursor->at++;
		}
	}
	if (*cursor->at == 'e' || *cursor->at == 'E') {
		cursor->at++;
		if (*cursor->at == '+' || *cursor->at == '-') {
			cursor->at++;
		}
		if (!isdigit((unsigned char)*cursor->at)) {
			return 0;
		}
		while (isdigit((unsigned char)*cursor->at)) {
			cursor->at++;
		}
	}
	return cursor->at > start;
}

static int smokeJsonParseValue(smoke_json_cursor_t *cursor);

static int smokeJsonEnter(smoke_json_cursor_t *cursor)
{
	if (cursor->depth >= SMOKE_JSON_MAX_DEPTH) {
		return 0;
	}
	cursor->depth++;
	return 1;
}

static int smokeJsonParseObject(smoke_json_cursor_t *cursor)
{
	if (*cursor->at != '{' || !smokeJsonEnter(cursor)) {
		return 0;
	}
	cursor->at++;
	smokeJsonSkipWhitespace(cursor);
	if (*cursor->at == '}') {
		cursor->at++;
		cursor->depth--;
		return 1;
	}

	for (;;) {
		if (!smokeJsonParseString(cursor)) {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
		if (*cursor->at++ != ':') {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
		if (!smokeJsonParseValue(cursor)) {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
		if (*cursor->at == '}') {
			cursor->at++;
			cursor->depth--;
			return 1;
		}
		if (*cursor->at++ != ',') {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
	}
}

static int smokeJsonParseArray(smoke_json_cursor_t *cursor)
{
	if (*cursor->at != '[' || !smokeJsonEnter(cursor)) {
		return 0;
	}
	cursor->at++;
	smokeJsonSkipWhitespace(cursor);
	if (*cursor->at == ']') {
		cursor->at++;
		cursor->depth--;
		return 1;
	}

	for (;;) {
		if (!smokeJsonParseValue(cursor)) {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
		if (*cursor->at == ']') {
			cursor->at++;
			cursor->depth--;
			return 1;
		}
		if (*cursor->at++ != ',') {
			return 0;
		}
		smokeJsonSkipWhitespace(cursor);
	}
}

static int smokeJsonParseLiteral(smoke_json_cursor_t *cursor,
		const char *literal)
{
	size_t length = strlen(literal);
	if (strncmp(cursor->at, literal, length)) {
		return 0;
	}
	cursor->at += length;
	return 1;
}

static int smokeJsonParseValue(smoke_json_cursor_t *cursor)
{
	smokeJsonSkipWhitespace(cursor);
	switch (*cursor->at) {
	case '{': return smokeJsonParseObject(cursor);
	case '[': return smokeJsonParseArray(cursor);
	case '"': return smokeJsonParseString(cursor);
	case 't': return smokeJsonParseLiteral(cursor, "true");
	case 'f': return smokeJsonParseLiteral(cursor, "false");
	case 'n': return smokeJsonParseLiteral(cursor, "null");
	default:
		return *cursor->at == '-' || isdigit((unsigned char)*cursor->at)
			? smokeJsonParseNumber(cursor) : 0;
	}
}

int smokeFixtureJsonValid(const char *json)
{
	smoke_json_cursor_t cursor;

	if (!json) {
		return 0;
	}
	cursor.at = json;
	cursor.depth = 0;
	smokeJsonSkipWhitespace(&cursor);
	if (!smokeJsonParseValue(&cursor)) {
		return 0;
	}
	smokeJsonSkipWhitespace(&cursor);
	return *cursor.at == '\0' && cursor.depth == 0;
}

smoke_fixture_field_mask_t smokeFixtureEventFieldFromName(const char *name)
{
	if (!name) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(k_event_fields) / sizeof(k_event_fields[0]); i++) {
		if (!strcmp(name, k_event_fields[i].name)) {
			return k_event_fields[i].field;
		}
	}
	return 0;
}

const char *smokeFixtureEventFieldName(smoke_fixture_field_mask_t field)
{
	for (size_t i = 0; i < sizeof(k_event_fields) / sizeof(k_event_fields[0]); i++) {
		if (field == k_event_fields[i].field) {
			return k_event_fields[i].name;
		}
	}
	return "unknown";
}

int smokeFixtureEventTypeKnown(const char *type)
{
	static const char *const types[] = {
		"", "wait", "wait_until", "exit", "unclean_exit", "screenshot",
		"receive_pdca_list", "agent_activate", "agent_delete",
		"catalog_recovery_probe", "catalog_weapon_acquire",
		"catalog_weapon_release", "key", "action", "mouse", "mouse_move",
		"mouse_wheel", "network_timeout_client", "network_reconnect",
	};

	if (!type) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		if (!strcmp(type, types[i])) {
			return 1;
		}
	}
	return 0;
}

smoke_fixture_field_mask_t smokeFixtureEventAllowedFields(const char *type)
{
	const smoke_fixture_field_mask_t common = SMOKE_FIXTURE_FIELD_AT_MS
		| SMOKE_FIXTURE_FIELD_TYPE | SMOKE_FIXTURE_FIELD_COMMENT;

	if (!type || !smokeFixtureEventTypeKnown(type)) {
		return 0;
	}
	if (!type[0] || !strcmp(type, "wait") || !strcmp(type, "exit")
			|| !strcmp(type, "unclean_exit")
			|| !strcmp(type, "network_reconnect")) {
		return common;
	}
	if (!strcmp(type, "network_timeout_client")) {
		return common | SMOKE_FIXTURE_FIELD_CLIENT_ID;
	}
	if (!strcmp(type, "wait_until")) {
		return common | SMOKE_FIXTURE_FIELD_CONDITION
			| SMOKE_FIXTURE_FIELD_TIMEOUT_MS | SMOKE_FIXTURE_FIELD_STABLE_MS
			| SMOKE_FIXTURE_FIELD_ASSIST_ACTION
			| SMOKE_FIXTURE_FIELD_ASSIST_CONDITION
			| SMOKE_FIXTURE_FIELD_ASSIST_HOLD_MS;
	}
	if (!strcmp(type, "screenshot") || !strcmp(type, "receive_pdca_list")
			|| !strcmp(type, "catalog_recovery_probe")
			|| !strcmp(type, "catalog_weapon_acquire")
			|| !strcmp(type, "catalog_weapon_release")) {
		return common | SMOKE_FIXTURE_FIELD_PATH;
	}
	if (!strcmp(type, "agent_activate") || !strcmp(type, "agent_delete")) {
		return common | SMOKE_FIXTURE_FIELD_NAME;
	}
	if (!strcmp(type, "key")) {
		return common | SMOKE_FIXTURE_FIELD_KEY | SMOKE_FIXTURE_FIELD_SCANCODE
			| SMOKE_FIXTURE_FIELD_ACTION;
	}
	if (!strcmp(type, "action")) {
		return common | SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_ACTION;
	}
	if (!strcmp(type, "mouse")) {
		return common | SMOKE_FIXTURE_FIELD_X | SMOKE_FIXTURE_FIELD_Y
			| SMOKE_FIXTURE_FIELD_BUTTON | SMOKE_FIXTURE_FIELD_ACTION;
	}
	if (!strcmp(type, "mouse_move")) {
		return common | SMOKE_FIXTURE_FIELD_X | SMOKE_FIXTURE_FIELD_Y;
	}
	if (!strcmp(type, "mouse_wheel")) {
		return common | SMOKE_FIXTURE_FIELD_WHEEL_X | SMOKE_FIXTURE_FIELD_WHEEL_Y;
	}
	return 0;
}

int smokeFixtureEventFieldsValid(const char *type,
		smoke_fixture_field_mask_t fields,
		smoke_fixture_field_mask_t *unsupported_fields)
{
	smoke_fixture_field_mask_t unsupported = fields
		& ~smokeFixtureEventAllowedFields(type);
	if (unsupported_fields) {
		*unsupported_fields = unsupported;
	}
	return smokeFixtureEventTypeKnown(type) && unsupported == 0;
}
