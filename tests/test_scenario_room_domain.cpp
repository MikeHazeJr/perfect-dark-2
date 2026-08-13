#include "catch.hpp"

extern "C" {
#include "scenario_room_domain.h"
}

#include <string>

TEST_CASE("scenario room domain preserves declared portal-only rooms",
	"[scenario-room-domain][t-tests-002]")
{
	int32_t rooms = 0;

	REQUIRE(scenarioRoomDomainResolve(147, 143, 146, &rooms)
		== SCENARIO_ROOM_DOMAIN_OK);
	REQUIRE(rooms == 147);
	REQUIRE(scenarioRoomDomainContains(rooms, 143) == 1);
	REQUIRE(scenarioRoomDomainContains(rooms, 144) == 1);
	REQUIRE(scenarioRoomDomainContains(rooms, 146) == 1);
	REQUIRE(scenarioRoomDomainContains(rooms, 147) == 0);
}

TEST_CASE("scenario room domain derives a compatibility count when undeclared",
	"[scenario-room-domain][t-tests-002]")
{
	int32_t rooms = 0;

	REQUIRE(scenarioRoomDomainResolve(0, 143, 146, &rooms)
		== SCENARIO_ROOM_DOMAIN_OK);
	REQUIRE(rooms == 147);

	REQUIRE(scenarioRoomDomainResolve(0, 0, -1, &rooms)
		== SCENARIO_ROOM_DOMAIN_OK);
	REQUIRE(rooms == SCENARIO_ROOM_DOMAIN_MIN_COUNT);
}

TEST_CASE("scenario room domain fails closed on undersized declarations",
	"[scenario-room-domain][t-tests-002]")
{
	int32_t rooms = 99;

	REQUIRE(scenarioRoomDomainResolve(144, 143, 146, &rooms)
		== SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL);
	REQUIRE(rooms == 0);

	REQUIRE(scenarioRoomDomainResolve(147, 147, 146, &rooms)
		== SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL);
	REQUIRE(rooms == 0);
}

TEST_CASE("scenario room domain enforces the native RoomNum boundary",
	"[scenario-room-domain][t-tests-002]")
{
	int32_t rooms = 0;

	REQUIRE(scenarioRoomDomainResolve(1, 0, -1, &rooms)
		== SCENARIO_ROOM_DOMAIN_INVALID_DECLARED_COUNT);
	REQUIRE(scenarioRoomDomainResolve(
		SCENARIO_ROOM_DOMAIN_MAX_COUNT + 1, 0, -1, &rooms)
		== SCENARIO_ROOM_DOMAIN_INVALID_DECLARED_COUNT);
	REQUIRE(scenarioRoomDomainResolve(0,
		SCENARIO_ROOM_DOMAIN_MAX_COUNT, -1, &rooms)
		== SCENARIO_ROOM_DOMAIN_OBSERVED_ROOM_OUT_OF_RANGE);
	REQUIRE(scenarioRoomDomainResolve(SCENARIO_ROOM_DOMAIN_MAX_COUNT,
		SCENARIO_ROOM_DOMAIN_MAX_COUNT - 1, -1, &rooms)
		== SCENARIO_ROOM_DOMAIN_OK);
	REQUIRE(rooms == SCENARIO_ROOM_DOMAIN_MAX_COUNT);
}

TEST_CASE("scenario room domain reports stable diagnostics",
	"[scenario-room-domain][t-tests-002]")
{
	REQUIRE(std::string(scenarioRoomDomainResultName(
		SCENARIO_ROOM_DOMAIN_DECLARED_COUNT_TOO_SMALL))
		== "declared_count_too_small");
	REQUIRE(std::string(scenarioRoomDomainResultName(
		SCENARIO_ROOM_DOMAIN_OBSERVED_ROOM_OUT_OF_RANGE))
		== "observed_room_out_of_range");
}
