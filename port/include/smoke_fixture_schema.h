#ifndef PD_SMOKE_FIXTURE_SCHEMA_H
#define PD_SMOKE_FIXTURE_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t smoke_fixture_field_mask_t;

/* Event type tokens are copied into the shipping harness only after this
 * shared bound admits the complete token. The capacity includes the trailing
 * NUL and must remain larger than every known event name. */
#define SMOKE_FIXTURE_EVENT_TYPE_CAPACITY 64u

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
	SMOKE_FIXTURE_FIELD_CLIENT_ID = 1u << 19,
	SMOKE_FIXTURE_FIELD_HAND = 1u << 20,
	SMOKE_FIXTURE_FIELD_AXIS = 1u << 21,
	SMOKE_FIXTURE_FIELD_VALUE = 1u << 22,
	SMOKE_FIXTURE_FIELD_SLOT = 1u << 23,
	SMOKE_FIXTURE_FIELD_TARGET = 1u << 24,
	SMOKE_FIXTURE_FIELD_DURATION_MS = 1u << 25
};

/* Pure fixture-schema boundary shared by the shipping harness and tests.
 * JSON remains author-friendly by allowing // line comments and hexadecimal
 * numbers, but malformed, truncated, or trailing documents fail closed. */
int smokeFixtureJsonValid(const char *json);

/* Raw virtual joystick axes use the complete Sint16 domain, including
 * -32768 for released triggers. Values must be checked before narrowing. */
int smokeFixtureControllerAxisValueValid(int64_t value);

int smokeFixtureEventTypeLengthValid(size_t length);
smoke_fixture_field_mask_t smokeFixtureEventFieldFromName(const char *name);
const char *smokeFixtureEventFieldName(smoke_fixture_field_mask_t field);
int smokeFixtureEventTypeKnown(const char *type);
smoke_fixture_field_mask_t smokeFixtureEventAllowedFields(const char *type);
int smokeFixtureEventFieldsValid(const char *type,
	smoke_fixture_field_mask_t fields,
	smoke_fixture_field_mask_t *unsupported_fields);

/* held-weapon-events.md lock smokes: an aim_at event's target and duration.
 * "simulant" is the nearest live simulant, "simulant:<i>" bot slot i (0 to
 * 31), "vehicle" the nearest vehicle. */
enum smoke_fixture_aim_target {
	SMOKE_FIXTURE_AIM_SIMULANT = 1,
	SMOKE_FIXTURE_AIM_SIMULANT_SLOT = 2,
	SMOKE_FIXTURE_AIM_VEHICLE = 3
};
#define SMOKE_FIXTURE_AIM_MAX_DURATION_MS 600000

/* 1 with the kind and the slot (-1 unless simulant:<i>) for a target in one
 * of those forms; 0 for any other text. */
int smokeFixtureAimTargetParse(const char *text, int *kind, int *slot);
/* An aim_at duration: above 0 and at most SMOKE_FIXTURE_AIM_MAX_DURATION_MS. */
int smokeFixtureAimDurationValid(int64_t ms);

#ifdef __cplusplus
}
#endif

#endif /* PD_SMOKE_FIXTURE_SCHEMA_H */
