/**
 * test_vehicle_seats.cpp -- the two-seat vehicle's rules that need no game
 * state (port/src/vehicle_seats.c): which seat a mount asks for, and the exit
 * button's single press (leave) and double press (switch seats).
 */
#include "catch.hpp"

#include <cmath>

extern "C" {
#include "vehicle_seats.h"
}

namespace {

const s32 kDriver = VEHICLE_GRAPH_SEAT_DRIVER;
const s32 kGunner = VEHICLE_GRAPH_SEAT_GUNNER;
const s32 kNone = VEHICLE_GRAPH_SEAT_NONE;
const float kTie = VEHICLE_SEAT_TIE_ANGLE;
const float kMargin = VEHICLE_SEAT_DRIVER_MARGIN;

vehicle_seat_candidate_t seat(bool exists, bool free, bool in_range, float view_angle, float dist)
{
	vehicle_seat_candidate_t s;
	s.exists = exists ? 1 : 0;
	s.free = free ? 1 : 0;
	s.in_range = in_range ? 1 : 0;
	s.view_angle = view_angle;
	s.dist = dist;
	return s;
}

s32 choose(const vehicle_seat_candidate_t &driver, const vehicle_seat_candidate_t &gunner)
{
	const vehicle_seat_candidate_t seats[VEHICLE_GRAPH_SEAT_COUNT] = { driver, gunner };
	return vehicleSeatChoose(seats, kTie, kMargin);
}

/* Degrees to the helper's radians, near enough for these cases. */
float rad(float deg)
{
	return deg * 3.14159265f / 180.0f;
}

} // namespace

TEST_CASE("The view angle to a seat is how far it is from where the player looks", "[vehicle-seats]")
{
	// vv_theta 0 looks along +z: a point ahead is at 0, to either side at a
	// quarter turn, behind at a half turn.
	CHECK(vehicleSeatViewAngle(0.0f, 100.0f, 0.0f) == Approx(0.0f).margin(0.002));
	CHECK(vehicleSeatViewAngle(100.0f, 0.0f, 0.0f) == Approx(rad(90.0f)).margin(0.002));
	CHECK(vehicleSeatViewAngle(-100.0f, 0.0f, 0.0f) == Approx(rad(90.0f)).margin(0.002));
	CHECK(vehicleSeatViewAngle(0.0f, -100.0f, 0.0f) == Approx(rad(180.0f)).margin(0.002));

	// The player's turn is counted the other way round from the bearing
	// (360 - vv_theta, as the interact cone does): vv_theta 270 looks along +x.
	CHECK(vehicleSeatViewAngle(100.0f, 0.0f, 270.0f) == Approx(0.0f).margin(0.002));
	CHECK(vehicleSeatViewAngle(-100.0f, 0.0f, 90.0f) == Approx(0.0f).margin(0.002));
	CHECK(vehicleSeatViewAngle(0.0f, -100.0f, 180.0f) == Approx(0.0f).margin(0.002));
	CHECK(vehicleSeatViewAngle(0.0f, 100.0f, 180.0f) == Approx(rad(180.0f)).margin(0.002));

	// Distance does not matter, and the answer never leaves 0..pi.
	CHECK(vehicleSeatViewAngle(1.0f, 1.0f, 0.0f) ==
		Approx(vehicleSeatViewAngle(500.0f, 500.0f, 0.0f)).margin(0.0005));
	for (int theta = 0; theta <= 360; theta += 15) {
		for (int bearing = 0; bearing < 360; bearing += 15) {
			const float dx = std::sin(rad(static_cast<float>(bearing))) * 200.0f;
			const float dz = std::cos(rad(static_cast<float>(bearing))) * 200.0f;
			const float angle = vehicleSeatViewAngle(dx, dz, static_cast<float>(theta));
			INFO("theta " << theta << " bearing " << bearing);
			CHECK(angle >= 0.0f);
			CHECK(angle <= 3.1416f);
			// The angle is the turn between the bearing and the view
			// direction (360 - theta), the short way round.
			float expect = std::fabs(static_cast<float>((bearing - (360 - theta)) % 360));
			if (expect > 180.0f) expect = 360.0f - expect;
			CHECK(angle == Approx(rad(expect)).margin(0.004));
		}
	}
}

TEST_CASE("A mount asks for the seat the player looks at", "[vehicle-seats]")
{
	// Both seats free and in range: the one looked at more directly.
	CHECK(choose(seat(true, true, true, 0.10f, 150.0f), seat(true, true, true, 0.40f, 120.0f)) == kDriver);
	CHECK(choose(seat(true, true, true, 0.40f, 120.0f), seat(true, true, true, 0.10f, 150.0f)) == kGunner);

	// Looking at the vehicle, not at a seat (within the tie angle): the
	// driver's seat, unless the gunner's is nearer by more than the margin.
	CHECK(choose(seat(true, true, true, 0.20f, 150.0f), seat(true, true, true, 0.22f, 120.0f)) == kGunner);
	CHECK(choose(seat(true, true, true, 0.22f, 120.0f), seat(true, true, true, 0.20f, 150.0f)) == kDriver);
	CHECK(choose(seat(true, true, true, 0.20f, 150.0f), seat(true, true, true, 0.22f, 140.0f)) == kDriver);
	CHECK(choose(seat(true, true, true, 0.20f, 150.0f),
		seat(true, true, true, 0.22f, 150.0f - kMargin - 1.0f)) == kGunner);
	CHECK(choose(seat(true, true, true, 0.20f, 150.0f),
		seat(true, true, true, 0.22f, 150.0f - kMargin + 1.0f)) == kDriver);
	// Beside the middle of the Maian vehicle, 150 units out, looking at its
	// origin: the seats are 17 and 11 degrees off the view and 157 and 153
	// units away. The driver's.
	CHECK(choose(seat(true, true, true, rad(16.7f), 156.6f), seat(true, true, true, rad(11.3f), 153.0f)) == kDriver);
	// Beside its pod, looking at its origin: 13 and 10 degrees, 183 and 153
	// units. The gunner's.
	CHECK(choose(seat(true, true, true, rad(13.2f), 183.1f), seat(true, true, true, rad(10.5f), 153.0f)) == kGunner);
	// Just past the tie angle the view decides again, whatever the distance.
	CHECK(choose(seat(true, true, true, 0.20f, 150.0f), seat(true, true, true, 0.20f + kTie + 0.01f, 10.0f)) == kDriver);
	CHECK(choose(seat(true, true, true, 0.20f + kTie + 0.01f, 10.0f), seat(true, true, true, 0.20f, 150.0f)) == kGunner);
	// The same angle and the same distance: the driver's.
	CHECK(choose(seat(true, true, true, 0.20f, 100.0f), seat(true, true, true, 0.20f, 100.0f)) == kDriver);
}

TEST_CASE("A taken seat gives way to the other seat when that one can be reached", "[vehicle-seats]")
{
	// Looking at the driver's seat, which is taken: the gunner's, if free and in range.
	CHECK(choose(seat(true, false, true, 0.10f, 150.0f), seat(true, true, true, 0.40f, 120.0f)) == kGunner);
	CHECK(choose(seat(true, true, true, 0.40f, 120.0f), seat(true, false, true, 0.10f, 150.0f)) == kDriver);
	// The other seat out of range, or taken too: no mount.
	CHECK(choose(seat(true, false, true, 0.10f, 150.0f), seat(true, true, false, 0.40f, 320.0f)) == kNone);
	CHECK(choose(seat(true, false, true, 0.10f, 150.0f), seat(true, false, true, 0.40f, 120.0f)) == kNone);
}

TEST_CASE("Only seats the vehicle has and the player can reach are chosen", "[vehicle-seats]")
{
	// A one-seat vehicle: the driver's seat, wherever the player looks.
	CHECK(choose(seat(true, true, true, 1.50f, 150.0f), seat(false, true, true, 0.01f, 10.0f)) == kDriver);
	CHECK(choose(seat(true, false, true, 0.10f, 150.0f), seat(false, true, true, 0.01f, 10.0f)) == kNone);
	// The seat looked at is out of range: the one in range.
	CHECK(choose(seat(true, true, false, 0.01f, 260.0f), seat(true, true, true, 0.90f, 150.0f)) == kGunner);
	CHECK(choose(seat(true, true, true, 0.90f, 150.0f), seat(true, true, false, 0.01f, 260.0f)) == kDriver);
	// Nothing in range, and no seats at all.
	CHECK(choose(seat(true, true, false, 0.0f, 400.0f), seat(true, true, false, 0.0f, 400.0f)) == kNone);
	CHECK(choose(seat(false, true, true, 0.0f, 1.0f), seat(false, true, true, 0.0f, 1.0f)) == kNone);
	CHECK(vehicleSeatChoose(nullptr, kTie, kMargin) == kNone);
}

TEST_CASE("One press of the exit button leaves; two in quick succession switch seats", "[vehicle-seats]")
{
	const s32 window = VEHICLE_EXIT_SWITCH_WINDOW60;
	vehicle_exit_press_t state{};

	SECTION("a rider who cannot switch leaves at once, as on a one-seat vehicle") {
		CHECK(vehicleExitPressStep(&state, 0, 0, 100, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 1, 0, 101, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
		CHECK(state.pending == 0);
		// Every press leaves; nothing is remembered between them.
		CHECK(vehicleExitPressStep(&state, 1, 0, 102, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
	}

	SECTION("a rider who can switch: a single press leaves when the window is over") {
		CHECK(vehicleExitPressStep(&state, 1, 1, 100, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(state.pending == 1);
		for (s32 frame = 101; frame < 100 + window; frame++) {
			INFO("frame " << frame);
			CHECK(vehicleExitPressStep(&state, 0, 1, frame, window) == VEHICLE_EXIT_PRESS_NONE);
		}
		CHECK(vehicleExitPressStep(&state, 0, 1, 100 + window, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
		CHECK(state.pending == 0);
		// Idle again afterwards.
		CHECK(vehicleExitPressStep(&state, 0, 1, 100 + window + 1, window) == VEHICLE_EXIT_PRESS_NONE);
	}

	SECTION("a second press inside the window switches seats") {
		CHECK(vehicleExitPressStep(&state, 1, 1, 100, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 0, 1, 105, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 1, 1, 109, window) == VEHICLE_EXIT_PRESS_SWITCH);
		CHECK(state.pending == 0);
		// The switch used both presses: nothing leaves afterwards.
		for (s32 frame = 110; frame < 110 + 2 * window; frame++) {
			CHECK(vehicleExitPressStep(&state, 0, 1, frame, window) == VEHICLE_EXIT_PRESS_NONE);
		}
		// The last frame of the window still switches; the frame after it leaves.
		CHECK(vehicleExitPressStep(&state, 1, 1, 200, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 1, 1, 200 + window - 1, window) == VEHICLE_EXIT_PRESS_SWITCH);
		CHECK(vehicleExitPressStep(&state, 1, 1, 300, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 1, 1, 300 + window, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
	}

	SECTION("the other seat is taken while a press waits: leave now") {
		CHECK(vehicleExitPressStep(&state, 1, 1, 100, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 0, 0, 103, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
		CHECK(state.pending == 0);
	}

	SECTION("a reset forgets a waiting press, and a frame counter that restarts ends the wait") {
		CHECK(vehicleExitPressStep(&state, 1, 1, 100, window) == VEHICLE_EXIT_PRESS_NONE);
		vehicleExitPressReset(&state);
		CHECK(state.pending == 0);
		for (s32 frame = 101; frame < 101 + 2 * window; frame++) {
			CHECK(vehicleExitPressStep(&state, 0, 1, frame, window) == VEHICLE_EXIT_PRESS_NONE);
		}
		CHECK(vehicleExitPressStep(&state, 1, 1, 5000, window) == VEHICLE_EXIT_PRESS_NONE);
		CHECK(vehicleExitPressStep(&state, 0, 1, 3, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
		vehicleExitPressReset(nullptr); // tolerated
	}

	SECTION("with no window or no state a press always leaves") {
		CHECK(vehicleExitPressStep(&state, 1, 1, 100, 0) == VEHICLE_EXIT_PRESS_DISMOUNT);
		CHECK(state.pending == 0);
		CHECK(vehicleExitPressStep(nullptr, 1, 1, 100, window) == VEHICLE_EXIT_PRESS_DISMOUNT);
		CHECK(vehicleExitPressStep(nullptr, 0, 1, 100, window) == VEHICLE_EXIT_PRESS_NONE);
	}
}

TEST_CASE("A point on the turret goes round the pivot as the turret turns", "[vehicle-seats]")
{
	// The Maian vehicle's gunner's eye is 300 units behind and above the yaw
	// pivot; the pivot is off the model's origin.
	const f32 pivot[3] = { 10.0f, 306.0f, -50.0f };
	const f32 eye[3] = { 10.0f, 960.0f, -350.0f };
	f32 out[3];

	// No turn: the point stays.
	vehicleTurretTurnPoint(pivot, eye, 0.0f, out);
	CHECK(out[0] == Approx(10.0f));
	CHECK(out[1] == Approx(960.0f));
	CHECK(out[2] == Approx(-350.0f));

	// A quarter turn: behind the pivot (-z) goes to its right (-x), as a point
	// ahead (+z) goes to its left (+x); the height stays.
	vehicleTurretTurnPoint(pivot, eye, 1.5707963f, out);
	CHECK(out[0] == Approx(10.0f - 300.0f).margin(0.01));
	CHECK(out[1] == Approx(960.0f));
	CHECK(out[2] == Approx(-50.0f).margin(0.01));
	const f32 ahead[3] = { 10.0f, 0.0f, 50.0f };
	vehicleTurretTurnPoint(pivot, ahead, 1.5707963f, out);
	CHECK(out[0] == Approx(110.0f).margin(0.01));
	CHECK(out[2] == Approx(-50.0f).margin(0.01));

	// Half a turn puts it ahead; the distance to the pivot never changes.
	vehicleTurretTurnPoint(pivot, eye, 3.1415927f, out);
	CHECK(out[0] == Approx(10.0f).margin(0.01));
	CHECK(out[2] == Approx(250.0f).margin(0.01));
	for (int step = 0; step < 16; step++) {
		vehicleTurretTurnPoint(pivot, eye, step * 0.4f - 3.0f, out);
		CHECK(std::hypot(out[0] - pivot[0], out[2] - pivot[2]) == Approx(300.0f).margin(0.01));
		CHECK(out[1] == Approx(960.0f));
	}

	// The same turn as the game's matrix: column 0 is (cos, 0, -sin) and
	// column 2 is (sin, 0, cos).
	const f32 yaw = 0.7f;
	const f32 side[3] = { 10.0f + 1.0f, 0.0f, -50.0f };
	vehicleTurretTurnPoint(pivot, side, yaw, out);
	CHECK(out[0] - pivot[0] == Approx(std::cos(yaw)));
	CHECK(out[2] - pivot[2] == Approx(-std::sin(yaw)));
}
