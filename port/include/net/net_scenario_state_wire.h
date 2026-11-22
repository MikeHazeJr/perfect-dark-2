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
 * Every objective scenario has a body: King of the Hill, Pop a Cap, Hold the
 * Briefcase, Hacker Central and Capture the Case. Combat has no scenario
 * state and is never sent.
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
	/* 1 while two or more teams are in the hill (the hold timer is paused),
	 * else 0. Lets a receiver run the timer between snapshots. */
	u8 contested;
	/* Team that scored the most recent hill point: -1 before the first, else
	 * 0..NET_SCENARIO_STATE_MAX_TEAMS-1. */
	s8 scoring_team;
	/* Hill points scored so far this match. A receiver that sees it rise
	 * presents the point (sound and message), as the host did. */
	u16 scores;
} net_scenario_koh_state_t;

/* Equal to MAX_MPCHRS (players plus simulants); the game-side adapter
 * asserts that. Per-character counts are sent for every slot. */
#define NET_SCENARIO_STATE_MAX_CHRS 40

/* Pop a Cap: survival lasts this long before it earns a point (60 s in
 * 1/240 s units); age240 never exceeds it. */
#define NET_SCENARIO_PAC_MAX_AGE240 (240 * 60)

typedef struct net_scenario_pac_state {
	/* Character slot of the current victim: -1 before the first, else
	 * 0..NET_SCENARIO_STATE_MAX_CHRS-1. */
	s8 victim;
	/* Goes up by one (wrapping) every time a new victim is chosen, so a
	 * receiver announces each victim once even if the same slot comes round
	 * again. */
	u8 victim_serial;
	/* The victim's survival time so far in 1/240 s units. */
	u16 age240;
	/* Victims popped by each character (two points each) and survival points
	 * earned by each, by character slot. Never negative. */
	s16 killcounts[NET_SCENARIO_STATE_MAX_CHRS];
	s16 survivalcounts[NET_SCENARIO_STATE_MAX_CHRS];
} net_scenario_pac_state_t;

typedef struct net_scenario_htb_state {
	/* Character slot holding the briefcase: -1 while it lies on the ground
	 * or before it exists, else 0..NET_SCENARIO_STATE_MAX_CHRS-1. */
	s8 holder;
	/* 1 once the briefcase exists (held or on the ground), else 0. */
	u8 present;
	/* Where it is: the holder's position while held. A receiver uses it only
	 * when it has no local prop to read the position from. */
	f32 pos[3];
} net_scenario_htb_state_t;

/* Equal to ARRAYCOUNT(scenariodata_htm.terminals); the game uses the first. */
#define NET_SCENARIO_HTM_MAX_TERMINALS 7

/* Hacker Central: a download completes after 20 s (1/240 s units). */
#define NET_SCENARIO_HTM_DOWNLOAD240 (240 * 20)

typedef struct net_scenario_htm_state {
	/* The data uplink, as net_scenario_htb_state_t's briefcase. */
	s8 uplink_holder;
	u8 uplink_present;
	f32 uplink_pos[3];
	/* Character slot downloading and the terminal it uses: both -1 when no
	 * download runs, else a slot and 0..NET_SCENARIO_HTM_MAX_TERMINALS-1. */
	s8 dl_player;
	s8 dl_terminal;
	/* Character slot within range of the terminal, -1 for none. */
	s8 player_in_range;
	/* Download progress (1/240 s units, at most NET_SCENARIO_HTM_DOWNLOAD240)
	 * and completed downloads, by character slot. */
	u16 dltime240[NET_SCENARIO_STATE_MAX_CHRS];
	s16 numpoints[NET_SCENARIO_STATE_MAX_CHRS];
} net_scenario_htm_state_t;

/* Equal to the Capture the Case base slots and their respawn pads
 * (scenariodata_ctc.spawnpadsperteam[4].spawnpads[6]). */
#define NET_SCENARIO_CTC_BASES 4
#define NET_SCENARIO_CTC_SPAWNPADS 6

typedef struct net_scenario_ctc_state {
	/* Base slot of each team: -1 for a team with no players, else
	 * 0..NET_SCENARIO_CTC_BASES-1, each slot at most once. */
	s8 teamindexes[NET_SCENARIO_CTC_BASES];
	/* Each base slot's home pad (-1 none) and respawn pads (the first
	 * numspawnpads of spawnpads, -1 after them). Pads are stage data, the
	 * same on every peer. */
	s16 homepads[NET_SCENARIO_CTC_BASES];
	u8 numspawnpads[NET_SCENARIO_CTC_BASES];
	s16 spawnpads[NET_SCENARIO_CTC_BASES][NET_SCENARIO_CTC_SPAWNPADS];
	/* Each team's case: the character slot holding it (-1 while it lies on
	 * the ground or does not exist), whether it exists, and where it is. */
	s8 holders[NET_SCENARIO_CTC_BASES];
	u8 present[NET_SCENARIO_CTC_BASES];
	f32 pos[NET_SCENARIO_CTC_BASES][3];
} net_scenario_ctc_state_t;

typedef struct net_scenario_state {
	/* NET_SCENARIO_STATE_* id of the body that follows. */
	u8 scenario;
	/* Increases on every authoritative change, so a receiver can drop a
	 * snapshot older than the one it already applied. */
	u32 revision;
	union {
		net_scenario_koh_state_t koh;
		net_scenario_pac_state_t pac;
		net_scenario_htb_state_t htb;
		net_scenario_htm_state_t htm;
		net_scenario_ctc_state_t ctc;
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
	/* gamemode-graphs.md slice 5: a section count outside 1 ..
	 * NET_SCENARIO_STATE_MAX_SECTIONS, two sections of one state, or sections
	 * that disagree on the revision. */
	NET_SCENARIO_STATE_BAD_SECTIONS,
} net_scenario_state_status_e;

/*
 * gamemode-graphs.md slice 5 (protocol v67): the message carries one section
 * per mode state the running mode's program uses (the owners of its plan),
 * each a whole state payload as above, keyed by the state, not by the mode
 * number: a base mode sends its own state alone, a mode that runs King of the
 * Hill's hill and Hold the Briefcase's token would send both. Sections are
 * distinct states and share one revision.
 */
#define NET_SCENARIO_STATE_MAX_SECTIONS 5

typedef struct net_scenario_state_list {
	u8 count;
	net_scenario_state_t sections[NET_SCENARIO_STATE_MAX_SECTIONS];
} net_scenario_state_list_t;

/* Every section valid, the count in range, the states distinct, one revision. */
net_scenario_state_status_e netScenarioStateListValidate(const net_scenario_state_list_t *list);

/* u8 count, then each section's payload. Writes nothing usable and sets the
 * buffer error when the list is invalid or does not fit. Returns 1 or 0. */
s32 netScenarioStateListWrite(struct netbuf *dst, const net_scenario_state_list_t *list);

/* Decodes and validates the whole list before publishing it: on any failure
 * *out is left exactly as it was and the buffer error is set. */
net_scenario_state_status_e netScenarioStateListRead(struct netbuf *src,
		net_scenario_state_list_t *out);

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
