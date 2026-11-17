/**
 * vehicle_seats.h -- the rules of a two-seat vehicle that need no game state.
 *
 * Mike's decisions for the Maian hover vehicle's seats (2026-11-16,
 * context/designs/modding/maian-vehicle-and-veteran.md, "V4 design"):
 *
 *   "The seat you take depends on where you entered from and where you're
 *    looking."
 *   "If no one is in the other seat, double-tapping the enter/exit vehicle
 *    button switches seats instead of exiting."
 *
 * The game (src/game/propobj.c, src/game/bondbike.c) measures; these functions
 * decide, so pd-tests can prove the decisions ([vehicle-seats]):
 *
 *   vehicleSeatViewAngle   how far a seat is from where the player looks
 *   vehicleSeatChoose      the seat a mount asks for
 *   vehicleExitPressStep   one press of the exit button leaves the vehicle,
 *                          two in quick succession switch seats
 *
 * Pure: no globals, no game headers beyond the seat numbers.
 */
#ifndef _IN_VEHICLE_SEATS_H
#define _IN_VEHICLE_SEATS_H

#include <PR/ultratypes.h>

#include "vehicle_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Two view angles this close (radians, about 9 degrees) count as the same:
 * the player is looking at the vehicle, not at one of its seats, and where
 * they stand decides. A player who looks at the vehicle's middle from beside
 * it sees both seats within a few degrees of each other. */
#define VEHICLE_SEAT_TIE_ANGLE 0.15f

/* When the view does not decide, the driver's seat is the one a player gets,
 * unless the gunner's is nearer by more than this (world units): beside the
 * middle of the vehicle the two seats are almost equally far, and a player
 * there wants to drive. Beside the pod, or behind the vehicle, the gunner's
 * seat is clearly nearer. */
#define VEHICLE_SEAT_DRIVER_MARGIN 20.0f

/* The exit button's second press must come within this many 60 Hz frames of
 * the first (0.3 s) to switch seats. */
#define VEHICLE_EXIT_SWITCH_WINDOW60 18

/* What the game measured for one seat when a player asks to mount. */
typedef struct vehicle_seat_candidate {
	s32 exists;      /* the vehicle has this seat (its graph mounts it) */
	s32 free;        /* nobody sits in it */
	s32 in_range;    /* its mount point is within the mount range */
	f32 view_angle;  /* radians from the player's view to it, 0 = straight at it */
	f32 dist;        /* horizontal distance from the player to its mount point */
} vehicle_seat_candidate_t;

/*
 * The angle between where the player looks and the direction to a point, in
 * radians from 0 (straight at it) to pi (straight away from it). dx and dz
 * are the point minus the player; view_theta_deg is the player's vv_theta.
 * The same formula as the interact cone (objTestForInteract), with the
 * original's constants, so "looking at the seat" and "looking at the vehicle"
 * agree.
 */
f32 vehicleSeatViewAngle(f32 dx, f32 dz, f32 view_theta_deg);

/*
 * The seat a mount asks for, or VEHICLE_GRAPH_SEAT_NONE.
 *
 * Among the seats that exist and are in range, the player prefers the one
 * they look at most directly; when the two view angles are within tie_angle
 * of each other, the driver's, unless the gunner's is nearer by more than
 * driver_margin. A preferred seat that is taken gives way to the other seat
 * when that one is free (and exists and is in range). With no free seat in
 * range the answer is NONE and the mount fails as it does today.
 */
s32 vehicleSeatChoose(const vehicle_seat_candidate_t seats[VEHICLE_GRAPH_SEAT_COUNT],
	f32 tie_angle, f32 driver_margin);

/* What a step of the exit button asks the game to do now. */
#define VEHICLE_EXIT_PRESS_NONE     0 /* nothing */
#define VEHICLE_EXIT_PRESS_DISMOUNT 1 /* leave the vehicle */
#define VEHICLE_EXIT_PRESS_SWITCH   2 /* move to the other seat */

/* One rider's exit button between presses. Zeroed is idle. */
typedef struct vehicle_exit_press {
	s32 pending;  /* a first press is waiting for a second */
	s32 since60;  /* the 60 Hz frame of that press */
} vehicle_exit_press_t;

/* Forget a waiting press (a mount, a dismount, a seat switch, a new stage). */
void vehicleExitPressReset(vehicle_exit_press_t *state);

/*
 * One update of a rider's exit button. pressed: the button went down this
 * update. can_switch: the rider could switch seats right now (the vehicle has
 * another seat, it is free, and the game allows a switch). now60: the 60 Hz
 * frame counter. window60: how long a first press waits for a second.
 *
 *   idle,    press, cannot switch   leave now, as a one-seat vehicle does
 *   idle,    press, can switch      wait for a second press
 *   waiting, press inside window    switch seats
 *   waiting, window over            leave (the single press, a moment late)
 *   waiting, can no longer switch   leave now (the other seat was taken)
 *
 * So only a rider who could switch waits the window before leaving.
 */
s32 vehicleExitPressStep(vehicle_exit_press_t *state, s32 pressed, s32 can_switch,
	s32 now60, s32 window60);

#ifdef __cplusplus
}
#endif

#endif /* _IN_VEHICLE_SEATS_H */
