/**
 * spectator.h -- Phase 3 spectator + Theater unified subsystem.
 *
 * Per Q12 ("Spectator + Theater unified subsystem.  One camera + control
 * + UI; two drivers (live stream / saved-match file). Don't build Theater
 * twice."): the camera / D-pad scheme / first-person toggle / free-fly
 * detach lives once in this module and is fed by one of two drivers:
 *
 *   - SPECTATOR_SOURCE_LIVE     -- subscribe to an authoritative match
 *                                  host's SVC_* state stream and replay
 *                                  positions in real time.
 *   - SPECTATOR_SOURCE_THEATER  -- read back a saved match file (Phase
 *                                  3 follow-up; the recorder + replay
 *                                  format are documented but not
 *                                  required to ship Phase 3 spectator).
 *
 * Phase 3 ship target: live spectator wired end-to-end. Theater driver
 * ships when the recorder lands -- the camera/control/UI architecture
 * is identical so the second driver only needs to feed the same
 * spectator_state_t.
 *
 * Connected client wires into someone else's match without taking a
 * slot. Read-only state stream from the host. Bandwidth: stream from
 * match host (extra outbound load on host); per-spectator bandwidth
 * budget documented inline in the host-side fan-out.
 *
 * Late-join: spectator connecting mid-match gets a state snapshot then
 * incremental updates (re-uses SVC_STAGE_START semantics).
 */

#ifndef _IN_SPECTATOR_H
#define _IN_SPECTATOR_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Source + camera + subset enums
 * ------------------------------------------------------------------------- */

typedef enum {
	SPECTATOR_SOURCE_NONE    = 0,
	SPECTATOR_SOURCE_LIVE    = 1,
	SPECTATOR_SOURCE_THEATER = 2,
} spectator_source_t;

typedef enum {
	SPECTATOR_CAM_THIRD_PERSON = 0, /* default; follows spectated chr */
	SPECTATOR_CAM_FIRST_PERSON = 1, /* R3 toggles to/from this */
	SPECTATOR_CAM_FREE_FLY     = 2, /* hold-Y detaches; release re-attaches */
} spectator_camera_t;

typedef enum {
	SPECTATOR_SUBSET_PLAYERS    = 0, /* humans only */
	SPECTATOR_SUBSET_ALL        = 1, /* humans + bots */
	SPECTATOR_SUBSET_TEAM_RED   = 2,
	SPECTATOR_SUBSET_TEAM_GREEN = 3,
	SPECTATOR_SUBSET_TEAM_BLUE  = 4,
	SPECTATOR_SUBSET_TEAM_GOLD  = 5,
	SPECTATOR_SUBSET_COUNT      = 6,
} spectator_subset_t;

/* -------------------------------------------------------------------------
 * Per-participant state delivered by either driver
 * ------------------------------------------------------------------------- */

#define SPECTATOR_MAX_PARTICIPANTS  16
#define SPECTATOR_NAME_MAX          32

typedef struct spectator_participant_s {
	u8     in_use;
	u8     team;
	u8     is_bot;
	u8     _pad;
	s16    score;
	s16    deaths;
	f32    pos[3];
	f32    angle_theta;
	f32    angle_verta;
	u32    weapon_runtime_idx; /* renderer hint; resolved up the stack */
	char   name[SPECTATOR_NAME_MAX];
} spectator_participant_t;

/* -------------------------------------------------------------------------
 * Aggregate spectator state (read by the camera + UI)
 * ------------------------------------------------------------------------- */

typedef struct spectator_state_s {
	spectator_source_t  source;
	spectator_camera_t  camera;
	spectator_subset_t  subset;
	s32                 focus_idx;     /* index into participants[]; -1 = none */
	u32                 stream_token;  /* live source: server-issued auth token */
	u32                 host_handle;   /* live source: friend handle of match host */
	u8                  free_fly_active;
	u8                  late_join_pending;
	u8                  _pad[2];
	f32                 free_fly_pos[3];
	f32                 free_fly_angle_theta;
	f32                 free_fly_angle_verta;
	u32                 last_state_ms;
	spectator_participant_t participants[SPECTATOR_MAX_PARTICIPANTS];
} spectator_state_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void spectatorInit(void);
void spectatorShutdown(void);
void spectatorTick(void);

/** Read-only snapshot pointer; stable for the lifetime of the process. */
const spectator_state_t *spectatorGet(void);

/** True if any spectator session is active (live or theater). */
s32 spectatorIsActive(void);

/* -------------------------------------------------------------------------
 * Live driver
 *
 * Begin spectating a friend's match. Emits a CLC_SPECTATE_REQUEST to
 * the friend's host; on SVC_SPECTATE_ACK the spectator state machine
 * transitions to RECEIVING and incoming SVC_* stream packets feed into
 * the participant table.
 * ------------------------------------------------------------------------- */

s32  spectatorBeginLive(u32 host_friend_handle);

/** Theater driver entry point. Sets source = SPECTATOR_SOURCE_THEATER
 *  and resets the participant table; the Theater module then drives
 *  spectatorIngestParticipantSnapshot directly from a replay file. */
void spectatorBeginTheater(void);

void spectatorStop(void);

/**
 * Inbound state push. The wire layer calls this on receive. Single-writer
 * for spectator_state_t.participants; the live driver and the theater
 * driver both go through this exact entry point.
 */
void spectatorIngestParticipantSnapshot(const spectator_participant_t *participants,
                                         s32 count, u32 host_handle);

/* -------------------------------------------------------------------------
 * Camera + control inputs (called by UI layer; no actionmap binding so
 * Session B's ownership is preserved).
 * ------------------------------------------------------------------------- */

/** D-pad up/down: cycle subset. */
void spectatorCycleSubset(s32 dir);
/** D-pad left/right: cycle member within current subset. */
void spectatorCycleMember(s32 dir);
/** R3 click: toggle 1st/3rd person. */
void spectatorToggleFirstPerson(void);
/** Hold-Y press: enter free-fly. Release: re-attach to last spectated. */
void spectatorBeginFreeFly(void);
void spectatorEndFreeFly(void);
/** Mouse / right-stick free-fly steering. dt in seconds. */
void spectatorFreeFlyInput(f32 dx, f32 dy, f32 dz, f32 dyaw, f32 dpitch, f32 dt);

/* -------------------------------------------------------------------------
 * Subset / focus accessors
 * ------------------------------------------------------------------------- */

const char *spectatorSubsetName(spectator_subset_t s);
s32  spectatorSubsetSize(spectator_subset_t s);
const spectator_participant_t *spectatorFocused(void);

/* -------------------------------------------------------------------------
 * Host-side fan-out (Phase 3 follow-up wiring; the function is exposed
 * here so the live broadcast path can plug in cleanly when it lands).
 * ------------------------------------------------------------------------- */

/**
 * Returns 1 if the local match host should fan out the next state
 * frame to spectators. The spectator dispatch is rate-controlled at
 * the same per-tick boundary as the existing player fan-out so the
 * total bandwidth budget is bounded:
 *
 *   per-spectator outbound = participant_table_size * sizeof(snapshot)
 *                            * SPECTATOR_FANOUT_HZ
 *
 * For 16 participants * 92 bytes * 10 Hz = ~14 KB/s per spectator.
 */
s32 spectatorHostShouldBroadcastThisTick(void);

#define SPECTATOR_FANOUT_HZ 10

#ifdef __cplusplus
}
#endif

#endif /* _IN_SPECTATOR_H */
