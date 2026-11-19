#include "catch.hpp"

extern "C" {
#include "scenario_room_domain.h"
}

#include <cmath>
#include <limits>
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

TEST_CASE("candidate room bounds accept asymmetric world-space boxes and flat collision",
	"[scenario-room-domain][grid]")
{
	const scenario_room_bounds_t authored = {{-3100.0f, 50.0f, 725.0f}, {-900.0f, 650.0f, 2515.0f}};
	const scenario_room_bounds_t floor = {{-3100.0f, 50.0f, 725.0f}, {-900.0f, 50.0f, 2515.0f}};
	CHECK(scenarioRoomBoundsValid(&authored, 1));
	CHECK(scenarioRoomBoundsValid(&floor, 0));
	CHECK_FALSE(scenarioRoomBoundsValid(&floor, 1));
	CHECK(scenarioRoomBoundsContain(&authored, &floor));

	scenario_room_bounds_t rounded = floor;
	rounded.minimum[0] = std::nextafter(rounded.minimum[0], -std::numeric_limits<float>::infinity());
	rounded.maximum[2] = std::nextafter(rounded.maximum[2], std::numeric_limits<float>::infinity());
	CHECK(scenarioRoomBoundsContain(&authored, &rounded));

	scenario_room_bounds_t clipped = authored;
	clipped.minimum[0] += 25.0f;
	CHECK_FALSE(scenarioRoomBoundsContain(&clipped, &floor));
	scenario_room_bounds_t shifted = authored;
	shifted.minimum[2] += 25.0f;
	shifted.maximum[2] += 25.0f;
	CHECK_FALSE(scenarioRoomBoundsContain(&shifted, &floor));
}

TEST_CASE("candidate room bounds and light origins preserve safe native arithmetic",
	"[scenario-room-domain][grid]")
{
	const float largest = std::numeric_limits<float>::max();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const scenario_room_bounds_t invalid = {{nan, -1.0f, -1.0f}, {0.0f, 1.0f, 1.0f}};
	const scenario_room_bounds_t inverted = {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 1.0f}};
	const scenario_room_bounds_t huge_center = {{largest, -1.0f, -1.0f}, {largest, 1.0f, 1.0f}};
	const scenario_room_bounds_t huge_radius = {{-1.0e20f, -1.0f, -1.0f}, {1.0e20f, 1.0f, 1.0f}};
	CHECK_FALSE(scenarioRoomBoundsValid(&invalid, 1));
	CHECK_FALSE(scenarioRoomBoundsValid(&inverted, 0));
	CHECK_FALSE(scenarioRoomBoundsValid(&huge_center, 0));
	CHECK_FALSE(scenarioRoomBoundsValid(&huge_radius, 1));

	const float light_origin[] = {-2150.5f, 325.0f, 2170.0f};
	const float huge_origin[] = {2147483648.0f, 0.0f, 0.0f};
	const float overflow_with_local_corner[] = {2147483520.0f, 0.0f, 0.0f};
	const float invalid_origin[] = {nan, 0.0f, 0.0f};
	CHECK(scenarioRoomOriginValid(light_origin));
	CHECK_FALSE(scenarioRoomOriginValid(huge_origin));
	CHECK_FALSE(scenarioRoomOriginValid(overflow_with_local_corner));
	CHECK_FALSE(scenarioRoomOriginValid(invalid_origin));
}
