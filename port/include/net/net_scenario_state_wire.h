#ifndef PD_NET_SCENARIO_STATE_WIRE_H
#define PD_NET_SCENARIO_STATE_WIRE_H

/*
 * Wire form of authoritative Combat Simulator scenario state.
 *
 * The listen host runs the scenario rules; every other peer presents the
 * host's state. This module is the pure data half of that seam: plain structs
 * with no pointers and no game globals, an encoder, and a decoder that either
 * returns a fully validated state or leaves its output untouched. The
 * game-side capture and apply between g_ScenarioData and these structs lives
 * with the scenario code, not here, so this file links into pd-tests as is.
 *
 * Scenarios are added one body at a time. King of the Hill is the first.
 * Combat has no scenario state and is never sent.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct netbuf;

/* Equal to the MPSCENARIO_* values; the game-side adapter asserts that. */
#define NET_SCENARIO_STATE_COMBAT            0
#define NET_SCENARIO_STATE_HOLDTHEBRIEFCASE  1
#define NET_SCENARIO_STATE_HACKERCENTRAL     2
#define NET_SCENARIO_STATE_POPACAP           3
#define NET_SCENARIO_STATE_KINGOFTHEHILL     4
#define NET_SCENARIO_STATE_CAPTURETHECASE    5

/* Equal to MAX_TEAMS; the game-side adapter asserts that. */
#define NET_SCENARIO_STATE_MAX_TEAMS 8

/* A stage setup can declare at most this many hills (koh.hillpads). */
#define NET_SCENARIO_KOH_MAX_HILLS 9

typedef struct net_scenario_koh_state {
	/* Which declared hill is live: -1 before the first hill is chosen, else
	 * 0..NET_SCENARIO_KOH_MAX_HILLS-1. */
	s16 hill_index;
	/* Room that is the hill, as the host resolved it: -1 or a room number. */
	s16 hill_room;
	/* Hill marker position as the host resolved it (floor height included). */
	f32 hill_pos[3];
	/* Team holding the hill: -1 for none, else 0..NET_SCENARIO_STATE_MAX_TEAMS-1. */
	s8 occupied_team;
	/* Hold time so far in 1/240 s units. Never negative. */
	s16 elapsed240;
	/* 1 while the hill is fading out before it moves, else 0. */
	u8 move_hill;
	/* Raw hill time setting; seconds per point is this plus 10. */
	u8 hill_time;
} net_scenario_koh_state_t;

typedef struct net_scenario_state {
	/* NET_SCENARIO_STATE_* id of the body that follows. */
	u8 scenario;
	/* Increases on every authoritative change, so a receiver can drop a
	 * snapshot older than the one it already applied. */
	u32 revision;
	union {
		net_scenario_koh_state_t koh;
	} body;
} net_scenario_state_t;

typedef enum net_scenario_state_status {
	NET_SCENARIO_STATE_OK = 0,
	/* Null argument or a buffer already in error. */
	NET_SCENARIO_STATE_BAD_ARGUMENT,
	/* The payload ended before the body did. */
	NET_SCENARIO_STATE_TRUNCATED,
	/* A scenario id with no body: Combat, or an id this build does not know. */
	NET_SCENARIO_STATE_BAD_SCENARIO,
	/* A field outside its documented range, or a non-finite position. */
	NET_SCENARIO_STATE_BAD_FIELD,
} net_scenario_state_status_e;

/* Range checks shared by the encoder and the decoder. */
net_scenario_state_status_e netScenarioStateValidate(const net_scenario_state_t *state);

/* Payload only; the dispatcher owns the opcode. Writes nothing and sets the
 * buffer error when the state is invalid or does not fit. Returns 1 on
 * success, 0 on failure. */
s32 netScenarioStatePayloadWrite(struct netbuf *dst, const net_scenario_state_t *state);

/* Decodes and validates the whole payload before publishing it: on any
 * failure *out is left exactly as it was and the buffer error is set. */
net_scenario_state_status_e netScenarioStatePayloadRead(struct netbuf *src,
		net_scenario_state_t *out);

/* Number of payload bytes for a scenario id, or 0 if it has no body. */
u32 netScenarioStatePayloadSize(u8 scenario);

#ifdef __cplusplus
}
#endif

#endif
