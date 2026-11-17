/**
 * vehicle_seats.c -- the rules of a two-seat vehicle that need no game state:
 * see vehicle_seats.h.
 */
#include <PR/ultratypes.h>

#include "vehicle_seats.h"

/* The original's pi and tau (src/include/math.h: M_BADPI, M_BADTAU), which the
 * interact cone uses; kept here so this file needs no game header. */
#define VEHICLE_SEATS_PI  3.141092641f
#define VEHICLE_SEATS_TAU (VEHICLE_SEATS_PI * 2)

/* The bearing of (x, z) from +z towards +x. The client links the game's own
 * (src/game/atan2f.c, 0 up to a turn) and pd-tests the C library's (minus half
 * a turn up to half a turn); they agree up to a whole turn, which the fold
 * below removes. Declared here because the math.h the client build finds does
 * not declare it, and game/atan2f.h brings the game's types with it. */
float atan2f(float x, float z);

f32 vehicleSeatViewAngle(f32 dx, f32 dz, f32 view_theta_deg)
{
	f32 angle = atan2f(dx, dz) - (360.0f - view_theta_deg) * VEHICLE_SEATS_TAU / 360.0f;

	/* The difference of two angles each within one turn: fold it into one
	 * turn, then to the nearer side. */
	while (angle < 0.0f) {
		angle += VEHICLE_SEATS_TAU;
	}
	while (angle >= VEHICLE_SEATS_TAU) {
		angle -= VEHICLE_SEATS_TAU;
	}
	if (angle > VEHICLE_SEATS_PI) {
		angle = VEHICLE_SEATS_TAU - angle;
	}
	return angle;
}

/* A seat a mount could take, were it free. */
static s32 seatReachable(const vehicle_seat_candidate_t *seat)
{
	return seat->exists && seat->in_range;
}

s32 vehicleSeatChoose(const vehicle_seat_candidate_t seats[VEHICLE_GRAPH_SEAT_COUNT],
	f32 tie_angle)
{
	const vehicle_seat_candidate_t *driver;
	const vehicle_seat_candidate_t *gunner;
	s32 preferred;
	s32 other;

	if (!seats) {
		return VEHICLE_GRAPH_SEAT_NONE;
	}
	driver = &seats[VEHICLE_GRAPH_SEAT_DRIVER];
	gunner = &seats[VEHICLE_GRAPH_SEAT_GUNNER];

	if (!seatReachable(driver) && !seatReachable(gunner)) {
		return VEHICLE_GRAPH_SEAT_NONE;
	}
	if (!seatReachable(gunner)) {
		preferred = VEHICLE_GRAPH_SEAT_DRIVER;
	} else if (!seatReachable(driver)) {
		preferred = VEHICLE_GRAPH_SEAT_GUNNER;
	} else {
		const f32 diff = gunner->view_angle - driver->view_angle;
		if (diff < -tie_angle) {
			preferred = VEHICLE_GRAPH_SEAT_GUNNER; /* looked at more directly */
		} else if (diff > tie_angle) {
			preferred = VEHICLE_GRAPH_SEAT_DRIVER;
		} else {
			/* Looking between them: the nearer seat, the driver's on a tie. */
			preferred = gunner->dist < driver->dist
				? VEHICLE_GRAPH_SEAT_GUNNER : VEHICLE_GRAPH_SEAT_DRIVER;
		}
	}
	if (seats[preferred].free) {
		return preferred;
	}
	other = preferred == VEHICLE_GRAPH_SEAT_DRIVER
		? VEHICLE_GRAPH_SEAT_GUNNER : VEHICLE_GRAPH_SEAT_DRIVER;
	if (seatReachable(&seats[other]) && seats[other].free) {
		return other;
	}
	return VEHICLE_GRAPH_SEAT_NONE;
}

void vehicleExitPressReset(vehicle_exit_press_t *state)
{
	if (state) {
		state->pending = 0;
		state->since60 = 0;
	}
}

s32 vehicleExitPressStep(vehicle_exit_press_t *state, s32 pressed, s32 can_switch,
	s32 now60, s32 window60)
{
	if (!state) {
		/* No memory of a first press: every press leaves. */
		return pressed ? VEHICLE_EXIT_PRESS_DISMOUNT : VEHICLE_EXIT_PRESS_NONE;
	}
	if (!state->pending) {
		if (!pressed) {
			return VEHICLE_EXIT_PRESS_NONE;
		}
		if (!can_switch || window60 <= 0) {
			return VEHICLE_EXIT_PRESS_DISMOUNT;
		}
		state->pending = 1;
		state->since60 = now60;
		return VEHICLE_EXIT_PRESS_NONE;
	}

	/* A first press is waiting. */
	if (!can_switch) {
		vehicleExitPressReset(state);
		return VEHICLE_EXIT_PRESS_DISMOUNT;
	}
	/* A frame counter that went backwards (a new stage) ends the wait too. */
	if (now60 < state->since60 || now60 - state->since60 >= window60) {
		vehicleExitPressReset(state);
		return VEHICLE_EXIT_PRESS_DISMOUNT;
	}
	if (pressed) {
		vehicleExitPressReset(state);
		return VEHICLE_EXIT_PRESS_SWITCH;
	}
	return VEHICLE_EXIT_PRESS_NONE;
}
