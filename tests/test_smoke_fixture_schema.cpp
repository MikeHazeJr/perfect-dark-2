#include "catch.hpp"

#include <string>

extern "C" {
#include "smoke_fixture_schema.h"
}

TEST_CASE("smoke fixture JSON validator accepts the authored extension",
	"[smoke][schema][b1088]")
{
	REQUIRE(smokeFixtureJsonValid(
		"// fixture\n{\"at_ms\":0x10,\"items\":[true,false,null,1.5e2],\"text\":\"ok\\n\"}"));
	REQUIRE(smokeFixtureJsonValid("{}"));
	REQUIRE(smokeFixtureJsonValid("[]"));
}

TEST_CASE("smoke fixture JSON validator rejects truncated or ambiguous documents",
	"[smoke][schema][b1088]")
{
	REQUIRE_FALSE(smokeFixtureJsonValid(nullptr));
	REQUIRE_FALSE(smokeFixtureJsonValid("{\"input_sequence\":[{"));
	REQUIRE_FALSE(smokeFixtureJsonValid("{\"name\":\"unterminated}"));
	REQUIRE_FALSE(smokeFixtureJsonValid("{\"a\":1 \"b\":2}"));
	REQUIRE_FALSE(smokeFixtureJsonValid("{\"a\" 1}"));
	REQUIRE_FALSE(smokeFixtureJsonValid("{\"a\":1,}"));
	REQUIRE_FALSE(smokeFixtureJsonValid("{} trailing"));
	REQUIRE_FALSE(smokeFixtureJsonValid("[1,]"));
	REQUIRE_FALSE(smokeFixtureJsonValid("1."));
	REQUIRE_FALSE(smokeFixtureJsonValid("01"));
}

TEST_CASE("smoke event schema rejects known fields on incompatible event types",
	"[smoke][schema][b1088]")
{
	const smoke_fixture_field_mask_t common = SMOKE_FIXTURE_FIELD_AT_MS
		| SMOKE_FIXTURE_FIELD_TYPE | SMOKE_FIXTURE_FIELD_COMMENT;
	const smoke_fixture_field_mask_t assisted_wait = common
		| SMOKE_FIXTURE_FIELD_CONDITION | SMOKE_FIXTURE_FIELD_TIMEOUT_MS
		| SMOKE_FIXTURE_FIELD_STABLE_MS | SMOKE_FIXTURE_FIELD_ASSIST_ACTION
		| SMOKE_FIXTURE_FIELD_ASSIST_CONDITION
		| SMOKE_FIXTURE_FIELD_ASSIST_HOLD_MS;
	smoke_fixture_field_mask_t unsupported = 0;

	REQUIRE(smokeFixtureEventTypeKnown("wait_until"));
	REQUIRE(smokeFixtureEventFieldsValid("wait_until", assisted_wait,
		&unsupported));
	REQUIRE(unsupported == 0);

	REQUIRE_FALSE(smokeFixtureEventFieldsValid("wait_until",
		assisted_wait | SMOKE_FIXTURE_FIELD_ACTION
			| SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_KEY,
		&unsupported));
	REQUIRE(unsupported == (SMOKE_FIXTURE_FIELD_ACTION
		| SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_KEY));

	REQUIRE(smokeFixtureEventFieldsValid("action",
		common | SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_ACTION,
		&unsupported));
	REQUIRE(smokeFixtureEventFieldsValid("network_timeout_client",
		common | SMOKE_FIXTURE_FIELD_CLIENT_ID, &unsupported));
	REQUIRE(smokeFixtureEventFieldsValid("network_retire_held_weapon",
		common | SMOKE_FIXTURE_FIELD_CLIENT_ID | SMOKE_FIXTURE_FIELD_HAND,
		&unsupported));
	REQUIRE(smokeFixtureEventFieldsValid(
		"network_assert_retired_prop_absent", common, &unsupported));
	REQUIRE_FALSE(smokeFixtureEventFieldsValid(
		"network_assert_retired_prop_absent",
		common | SMOKE_FIXTURE_FIELD_CLIENT_ID, &unsupported));
	REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_CLIENT_ID);
	REQUIRE_FALSE(smokeFixtureEventFieldsValid("network_timeout_client",
		common | SMOKE_FIXTURE_FIELD_CLIENT_ID | SMOKE_FIXTURE_FIELD_HAND,
		&unsupported));
	REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_HAND);
	REQUIRE(smokeFixtureEventFieldsValid("network_reconnect", common,
		&unsupported));
	REQUIRE_FALSE(smokeFixtureEventFieldsValid("network_reconnect",
		common | SMOKE_FIXTURE_FIELD_CLIENT_ID, &unsupported));
	REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_CLIENT_ID);
	REQUIRE_FALSE(smokeFixtureEventFieldsValid("action",
		common | SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_PATH,
		&unsupported));
	REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_PATH);
	REQUIRE_FALSE(smokeFixtureEventFieldsValid("typo", common, &unsupported));
}

TEST_CASE("smoke event type admission never truncates known or oversized tokens",
	"[smoke][schema][b1088][b1096]")
{
	const std::string longest_known =
		"network_assert_retired_prop_absent";

	REQUIRE(longest_known.size() == 34);
	REQUIRE(smokeFixtureEventTypeLengthValid(longest_known.size()));
	REQUIRE(smokeFixtureEventTypeLengthValid(
		SMOKE_FIXTURE_EVENT_TYPE_CAPACITY - 1u));
	REQUIRE_FALSE(smokeFixtureEventTypeLengthValid(
		SMOKE_FIXTURE_EVENT_TYPE_CAPACITY));
}

TEST_CASE("smoke event field names resolve to one stable typed bit",
	"[smoke][schema][b1088]")
{
	REQUIRE(smokeFixtureEventFieldFromName("stable_ms") ==
		SMOKE_FIXTURE_FIELD_STABLE_MS);
	REQUIRE(smokeFixtureEventFieldFromName("comment") ==
		SMOKE_FIXTURE_FIELD_COMMENT);
	REQUIRE(smokeFixtureEventFieldFromName("client_id") ==
		SMOKE_FIXTURE_FIELD_CLIENT_ID);
	REQUIRE(smokeFixtureEventFieldFromName("hand") ==
		SMOKE_FIXTURE_FIELD_HAND);
	REQUIRE(smokeFixtureEventFieldFromName("typo") == 0);
	REQUIRE(std::string(smokeFixtureEventFieldName(
		SMOKE_FIXTURE_FIELD_ASSIST_ACTION)) == "assist_action");
}

TEST_CASE("virtual controller event schema separates lifecycle and typed state fields",
    "[smoke][schema][controller]")
{
    const smoke_fixture_field_mask_t common = SMOKE_FIXTURE_FIELD_AT_MS
        | SMOKE_FIXTURE_FIELD_TYPE | SMOKE_FIXTURE_FIELD_COMMENT;
    const smoke_fixture_field_mask_t button = common | SMOKE_FIXTURE_FIELD_BUTTON
        | SMOKE_FIXTURE_FIELD_ACTION;
    const smoke_fixture_field_mask_t axis = common | SMOKE_FIXTURE_FIELD_AXIS
        | SMOKE_FIXTURE_FIELD_VALUE;
    smoke_fixture_field_mask_t unsupported = 0;
    for (const char *kind : {"controller_attach", "controller_detach"}) {
        REQUIRE(smokeFixtureEventTypeKnown(kind));
        REQUIRE(smokeFixtureEventFieldsValid(kind, common, &unsupported));
        REQUIRE_FALSE(smokeFixtureEventFieldsValid(kind, button, &unsupported));
        REQUIRE_FALSE(smokeFixtureEventFieldsValid(kind, axis, &unsupported));
    }
    REQUIRE(smokeFixtureEventTypeKnown("controller_button"));
    REQUIRE(smokeFixtureEventFieldsValid("controller_button", button, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid("controller_button", axis, &unsupported));
    REQUIRE(smokeFixtureEventTypeKnown("controller_axis"));
    REQUIRE(smokeFixtureEventFieldsValid("controller_axis", axis, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid("controller_axis", button, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid("controller_axis",
        axis | SMOKE_FIXTURE_FIELD_NAME | SMOKE_FIXTURE_FIELD_CLIENT_ID, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid("action", axis, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid("mouse", axis, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventTypeKnown("controller_tap"));
    REQUIRE(smokeFixtureEventFieldFromName("axis") == SMOKE_FIXTURE_FIELD_AXIS);
    REQUIRE(smokeFixtureEventFieldFromName("value") == SMOKE_FIXTURE_FIELD_VALUE);
    REQUIRE(std::string(smokeFixtureEventFieldName(SMOKE_FIXTURE_FIELD_AXIS)) == "axis");
    REQUIRE(std::string(smokeFixtureEventFieldName(SMOKE_FIXTURE_FIELD_VALUE)) == "value");
}

TEST_CASE("virtual controller axis values are checked before narrowing",
    "[smoke][schema][controller]")
{
    REQUIRE(smokeFixtureControllerAxisValueValid(-32768));
    REQUIRE(smokeFixtureControllerAxisValueValid(0));
    REQUIRE(smokeFixtureControllerAxisValueValid(32767));
    REQUIRE_FALSE(smokeFixtureControllerAxisValueValid(-32769));
    REQUIRE_FALSE(smokeFixtureControllerAxisValueValid(32768));
    REQUIRE_FALSE(smokeFixtureControllerAxisValueValid(INT64_MIN));
    REQUIRE_FALSE(smokeFixtureControllerAxisValueValid(INT64_MAX));
    REQUIRE_FALSE(smokeFixtureControllerAxisValueValid(INT64_C(4294967296)));
}

TEST_CASE("Prepared Apply core probe accepts no input or path payload",
    "[smoke][schema][modmgr][core]")
{
    const char *type = "modmgr_apply_retry_probe";
    REQUIRE(smokeFixtureEventTypeKnown(type));
    const auto common = SMOKE_FIXTURE_FIELD_AT_MS | SMOKE_FIXTURE_FIELD_TYPE | SMOKE_FIXTURE_FIELD_COMMENT;
    REQUIRE(smokeFixtureEventAllowedFields(type) == common);
    smoke_fixture_field_mask_t unsupported = 0;
    REQUIRE(smokeFixtureEventFieldsValid(type, common, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid(type, common | SMOKE_FIXTURE_FIELD_PATH, &unsupported));
    REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_PATH);
    REQUIRE_FALSE(smokeFixtureEventFieldsValid(type, common | SMOKE_FIXTURE_FIELD_ACTION, &unsupported));
    REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_ACTION);
}

TEST_CASE("File transfer socket probe cannot name an arbitrary local path",
    "[smoke][schema][net][file-transfer]")
{
    const char *type = "file_transfer_socket_probe";
    const auto common = SMOKE_FIXTURE_FIELD_AT_MS | SMOKE_FIXTURE_FIELD_TYPE | SMOKE_FIXTURE_FIELD_COMMENT;
    REQUIRE(smokeFixtureEventTypeKnown(type));
    REQUIRE(smokeFixtureEventAllowedFields(type) == common);
    smoke_fixture_field_mask_t unsupported = 0;
    REQUIRE(smokeFixtureEventFieldsValid(type, common, &unsupported));
    REQUIRE_FALSE(smokeFixtureEventFieldsValid(type, common | SMOKE_FIXTURE_FIELD_PATH, &unsupported));
    REQUIRE(unsupported == SMOKE_FIXTURE_FIELD_PATH);
}
