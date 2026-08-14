#ifndef PD_SMOKE_FIXTURE_SCHEMA_H
#define PD_SMOKE_FIXTURE_SCHEMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t smoke_fixture_field_mask_t;

enum smoke_fixture_event_field {
	SMOKE_FIXTURE_FIELD_AT_MS = 1u << 0,
	SMOKE_FIXTURE_FIELD_TYPE = 1u << 1,
	SMOKE_FIXTURE_FIELD_KEY = 1u << 2,
	SMOKE_FIXTURE_FIELD_SCANCODE = 1u << 3,
	SMOKE_FIXTURE_FIELD_ACTION = 1u << 4,
	SMOKE_FIXTURE_FIELD_NAME = 1u << 5,
	SMOKE_FIXTURE_FIELD_CONDITION = 1u << 6,
	SMOKE_FIXTURE_FIELD_TIMEOUT_MS = 1u << 7,
	SMOKE_FIXTURE_FIELD_STABLE_MS = 1u << 8,
	SMOKE_FIXTURE_FIELD_ASSIST_ACTION = 1u << 9,
	SMOKE_FIXTURE_FIELD_ASSIST_CONDITION = 1u << 10,
	SMOKE_FIXTURE_FIELD_ASSIST_HOLD_MS = 1u << 11,
	SMOKE_FIXTURE_FIELD_PATH = 1u << 12,
	SMOKE_FIXTURE_FIELD_X = 1u << 13,
	SMOKE_FIXTURE_FIELD_Y = 1u << 14,
	SMOKE_FIXTURE_FIELD_BUTTON = 1u << 15,
	SMOKE_FIXTURE_FIELD_WHEEL_X = 1u << 16,
	SMOKE_FIXTURE_FIELD_WHEEL_Y = 1u << 17,
	SMOKE_FIXTURE_FIELD_COMMENT = 1u << 18,
	SMOKE_FIXTURE_FIELD_CLIENT_ID = 1u << 19
};

/* Pure fixture-schema boundary shared by the shipping harness and tests.
 * JSON remains author-friendly by allowing // line comments and hexadecimal
 * numbers, but malformed, truncated, or trailing documents fail closed. */
int smokeFixtureJsonValid(const char *json);

smoke_fixture_field_mask_t smokeFixtureEventFieldFromName(const char *name);
const char *smokeFixtureEventFieldName(smoke_fixture_field_mask_t field);
int smokeFixtureEventTypeKnown(const char *type);
smoke_fixture_field_mask_t smokeFixtureEventAllowedFields(const char *type);
int smokeFixtureEventFieldsValid(const char *type,
	smoke_fixture_field_mask_t fields,
	smoke_fixture_field_mask_t *unsupported_fields);

#ifdef __cplusplus
}
#endif

#endif /* PD_SMOKE_FIXTURE_SCHEMA_H */
