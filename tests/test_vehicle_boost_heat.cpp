/**
 * test_vehicle_boost_heat.cpp -- the Maian vehicle's boost and heat (V3).
 *
 * The state machine in port/src/vehicle_boost_heat.c is the proposed
 * og.vehicle.boost_heat module (context/designs/modding/vehicle-graph-v1.md).
 * Each step here is one 60 Hz frame (4 ticks at 240 Hz), so with the default
 * params: full boost after 60 frames (1 s), black crystals after 240 frames
 * of boost (4 s), cool again 120 frames (2 s) after letting go when stopped.
 */
#include "catch.hpp"

#include <cstring>

extern "C" {
#include "vehicle_boost_heat.h"
}

namespace {

constexpr float kFrame = 4.0f; /* one 60 Hz frame in 240 Hz ticks */

struct Boost {
	vehicle_boost_heat_params_t p;
	vehicle_boost_heat_state_t s;
	int changes = 0;
	int last_prev = -1;

	Boost()
	{
		vehicleBoostHeatDefaultParams(&p);
		vehicleBoostHeatReset(&s);
	}

	void frame(bool held, float speed = 0.0f)
	{
		s32 prev = -1;
		if (vehicleBoostHeatStep(&s, &p, held ? 1 : 0, speed, kFrame, &prev)) {
			changes++;
			last_prev = prev;
		}
	}

	/* Frames until the phase is `phase` (counting the frame that reaches it), or -1. */
	int framesUntil(bool held, int phase, int limit, float speed = 0.0f)
	{
		for (int i = 1; i <= limit; i++) {
			frame(held, speed);
			if (s.phase == phase) {
				return i;
			}
		}
		return -1;
	}

	void overheat()
	{
		REQUIRE(framesUntil(true, VEHICLE_BOOST_HEAT_OVERHEATED, 1000) == 240);
	}
};

} // namespace

TEST_CASE("Maian boost ramps to full in about a second", "[vehicle-boost-heat]")
{
	Boost b;
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_COOL);
	REQUIRE(vehicleBoostHeatExtraSpeed(&b.s, &b.p) == 0.0f);

	b.frame(true);
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_RAMPING);
	REQUIRE_FALSE(vehicleBoostHeatTurretLocked(&b.s));

	REQUIRE(b.framesUntil(true, VEHICLE_BOOST_HEAT_FULL, 1000) == 59);
	REQUIRE(b.s.boost == 1.0f);
	REQUIRE(vehicleBoostHeatExtraSpeed(&b.s, &b.p) == Approx(7.0f));
	/* At full boost the turret cannot fire (Mike's lock-on rule). */
	REQUIRE(vehicleBoostHeatTurretLocked(&b.s));
	REQUIRE_FALSE(vehicleBoostHeatCrystalsBlack(&b.s));
}

TEST_CASE("The crystals turn black after about four seconds of boost", "[vehicle-boost-heat]")
{
	Boost b;
	b.overheat();
	REQUIRE(b.last_prev == VEHICLE_BOOST_HEAT_FULL);
	REQUIRE(vehicleBoostHeatCrystalsBlack(&b.s));
	REQUIRE(vehicleBoostHeatTurretLocked(&b.s));

	/* Steering is limited briefly: 45 frames, then back to normal. */
	REQUIRE(vehicleBoostHeatSteerScale(&b.s, &b.p) == Approx(0.5f));
	for (int i = 0; i < 44; i++) b.frame(true);
	REQUIRE(vehicleBoostHeatSteerScale(&b.s, &b.p) == Approx(0.5f));
	b.frame(true);
	REQUIRE(vehicleBoostHeatSteerScale(&b.s, &b.p) == 1.0f);

	/* Boost continues above base but below full: down to the cap and kept there. */
	for (int i = 0; i < 1000; i++) b.frame(true);
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_OVERHEATED);
	REQUIRE(b.s.heat == 1.0f);
	REQUIRE(b.s.boost == Approx(0.5f));
	REQUIRE(vehicleBoostHeatExtraSpeed(&b.s, &b.p) == Approx(3.5f));
}

TEST_CASE("Letting go recovers in two seconds when stopped and longer at speed", "[vehicle-boost-heat]")
{
	SECTION("stopped") {
		Boost b;
		b.overheat();
		b.frame(false);
		REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_RECOVERING);
		REQUIRE(b.last_prev == VEHICLE_BOOST_HEAT_OVERHEATED);
		REQUIRE(vehicleBoostHeatCrystalsBlack(&b.s));
		REQUIRE(vehicleBoostHeatTurretLocked(&b.s));
		REQUIRE(b.framesUntil(false, VEHICLE_BOOST_HEAT_COOL, 1000) == 119);
		REQUIRE(b.s.heat == 0.0f);
		REQUIRE_FALSE(vehicleBoostHeatCrystalsBlack(&b.s));
		REQUIRE_FALSE(vehicleBoostHeatTurretLocked(&b.s));
	}
	SECTION("half speed") {
		Boost b;
		b.overheat();
		REQUIRE(b.framesUntil(false, VEHICLE_BOOST_HEAT_COOL, 1000, 0.5f) == 180);
	}
	SECTION("full speed") {
		Boost b;
		b.overheat();
		REQUIRE(b.framesUntil(false, VEHICLE_BOOST_HEAT_COOL, 1000, 1.0f) == 240);
	}
}

TEST_CASE("Boosting while the crystals recover stays capped and does not lengthen it", "[vehicle-boost-heat]")
{
	Boost b;
	b.overheat();
	b.frame(false);
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_RECOVERING);
	const float after_release = b.s.boost;
	REQUIRE(after_release < 1.0f);

	int cool = -1;
	float highest = 0.0f;
	for (int i = 1; i <= 1000; i++) {
		b.frame(true);
		if (b.s.phase == VEHICLE_BOOST_HEAT_RECOVERING && i > 40) {
			highest = b.s.boost > highest ? b.s.boost : highest;
		}
		if (b.s.phase != VEHICLE_BOOST_HEAT_RECOVERING) {
			cool = i;
			break;
		}
	}
	/* 120 frames after letting go, the same as not boosting again. */
	REQUIRE(cool == 119);
	REQUIRE(highest == Approx(0.5f));
	/* Still held, so boosting starts again from the cap. */
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_RAMPING);
	REQUIRE_FALSE(vehicleBoostHeatCrystalsBlack(&b.s));
}

TEST_CASE("Letting go before overheating keeps the crystals normal", "[vehicle-boost-heat]")
{
	Boost b;
	for (int i = 0; i < 100; i++) b.frame(true);
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_FULL);
	REQUIRE(b.s.heat == Approx(100.0f / 240.0f));

	b.frame(false);
	REQUIRE(b.s.phase == VEHICLE_BOOST_HEAT_COOL);
	REQUIRE(b.last_prev == VEHICLE_BOOST_HEAT_FULL);
	for (int i = 0; i < 59; i++) b.frame(false);
	REQUIRE(b.s.boost == 0.0f);
	REQUIRE(b.s.heat == 0.0f);

	/* Heat left over from a short break counts towards the next overheat. */
	Boost c;
	for (int i = 0; i < 200; i++) c.frame(true);
	for (int i = 0; i < 10; i++) c.frame(false);
	REQUIRE(c.s.heat == Approx(200.0f / 240.0f - 10.0f / 120.0f));
	REQUIRE(c.framesUntil(true, VEHICLE_BOOST_HEAT_OVERHEATED, 1000) == 60);
}

TEST_CASE("Each phase change is reported once with the old phase", "[vehicle-boost-heat]")
{
	Boost b;
	int order[8];
	int n = 0;
	s32 prev = -1;
	auto step = [&](bool held) {
		if (vehicleBoostHeatStep(&b.s, &b.p, held ? 1 : 0, 0.0f, kFrame, &prev) && n < 8) {
			order[n++] = b.s.phase;
		}
	};
	for (int i = 0; i < 300; i++) step(true);
	for (int i = 0; i < 200; i++) step(false);
	REQUIRE(n == 5);
	REQUIRE(order[0] == VEHICLE_BOOST_HEAT_RAMPING);
	REQUIRE(order[1] == VEHICLE_BOOST_HEAT_FULL);
	REQUIRE(order[2] == VEHICLE_BOOST_HEAT_OVERHEATED);
	REQUIRE(order[3] == VEHICLE_BOOST_HEAT_RECOVERING);
	REQUIRE(order[4] == VEHICLE_BOOST_HEAT_COOL);
	REQUIRE(prev == VEHICLE_BOOST_HEAT_RECOVERING);
	REQUIRE(std::strcmp(vehicleBoostHeatPhaseName(VEHICLE_BOOST_HEAT_OVERHEATED), "overheated") == 0);
	REQUIRE(std::strcmp(vehicleBoostHeatPhaseName(99), "?") == 0);
}

TEST_CASE("Boost params are checked and times convert to 240 Hz ticks", "[vehicle-boost-heat]")
{
	vehicle_boost_heat_params_t p;
	const char *why = "unset";
	vehicleBoostHeatDefaultParams(&p);
	REQUIRE(vehicleBoostHeatParamsValid(&p, &why));
	REQUIRE(why == nullptr);

	vehicle_boost_heat_params_t bad = p;
	bad.ramp_ticks240 = 0.0f;
	REQUIRE_FALSE(vehicleBoostHeatParamsValid(&bad, &why));
	REQUIRE(std::strcmp(why, "ramp_time") == 0);
	bad = p;
	bad.overheated_boost = 1.5f;
	REQUIRE_FALSE(vehicleBoostHeatParamsValid(&bad, &why));
	REQUIRE(std::strcmp(why, "overheated_boost") == 0);
	bad = p;
	bad.recover_speed_scale = 0.0f;
	REQUIRE_FALSE(vehicleBoostHeatParamsValid(&bad, &why));
	REQUIRE(std::strcmp(why, "recover_speed_scale") == 0);

	float ticks = -1.0f;
	REQUIRE(vehicleBoostHeatTimeToTicks240(60.0f, "ticks60", &ticks));
	REQUIRE(ticks == 240.0f);
	REQUIRE(vehicleBoostHeatTimeToTicks240(1800.0f, "ticks240", &ticks));
	REQUIRE(ticks == 1800.0f);
	REQUIRE(vehicleBoostHeatTimeToTicks240(100.0f, "centiseconds", &ticks));
	REQUIRE(ticks == Approx(240.0f));
	REQUIRE_FALSE(vehicleBoostHeatTimeToTicks240(1.0f, "seconds", &ticks));
	REQUIRE_FALSE(vehicleBoostHeatTimeToTicks240(-1.0f, "ticks60", &ticks));
}
