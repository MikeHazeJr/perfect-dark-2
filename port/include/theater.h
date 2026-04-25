/**
 * theater.h -- Phase 3 saved-match recorder + replay reader.
 *
 * Per Q12: Theater feeds the same `spectatorIngestParticipantSnapshot`
 * entry point the live driver uses, so the camera + control + UI
 * surface in `port/src/spectator.c` and `port/fast3d/pdgui_spectator.cpp`
 * is shared one-for-one with replay playback.
 *
 * File format (.pdth, little-endian):
 *
 *   off len  field
 *   ----------------------------
 *    0  4    magic "PDTH"
 *    4  4    file version (u32)
 *    8  8    start_time_unix (u64)
 *   16  4    frame_count (u32, may be 0 if writer crashed)
 *   20  4    fanout_hz (u32, expected cadence)
 *   24  8    reserved (zero)
 *
 * Followed by a stream of records:
 *
 *   off len  field
 *   ----------------------------
 *    0  1    kind (0=state_frame, 1=match_config, 2=end)
 *    1  4    length (u32, payload bytes)
 *    5  N    payload
 *
 * State-frame payload matches the SVC_STATE_FRAME wire layout: u32
 * host_handle + u32 frame_seq + u8 participant_count + count *
 * 64-byte participant blocks. The reader unpacks identically.
 *
 * No compression in this version. Documented as a future polish: zlib
 * is already statically linked, and a one-line zlib pass over each
 * record would shrink replays by ~3x. See connectivity-phase4-decisions
 * for the deferral note when it lands.
 */

#ifndef _IN_THEATER_H
#define _IN_THEATER_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEATER_MAGIC                "PDTH"
#define THEATER_FILE_VERSION         1
#define THEATER_RECORD_STATE_FRAME   0
#define THEATER_RECORD_MATCH_CONFIG  1
#define THEATER_RECORD_END           2
#define THEATER_HEADER_LEN           32
#define THEATER_RECORD_HEADER_LEN    5

void theaterInit(void);
void theaterShutdown(void);
void theaterTick(void);

/* -------------------------------------------------------------------------
 * Recording
 * ------------------------------------------------------------------------- */

/**
 * Begin recording the local match. Filename is relative to
 * <home>/replays/. The host fan-out path (netSendSpectateStateFrame's
 * blob build) is sampled at SPECTATOR_FANOUT_HZ. Returns 0 on success.
 */
s32 theaterStartRecording(const char *filename);

/** Returns 1 while a recording is in flight. */
s32 theaterIsRecording(void);

/** Close the recording (writes end record, fsync). */
void theaterStopRecording(void);

/* -------------------------------------------------------------------------
 * Playback
 * ------------------------------------------------------------------------- */

/**
 * Open `filename` (relative to <home>/replays/) and begin feeding the
 * spectator subsystem. spectatorIsActive() reports true while the
 * playback runs. Returns 0 on success.
 */
s32 theaterStartReplay(const char *filename);

/** Close the replay (calls spectatorStop). */
void theaterStopReplay(void);

/** Returns 1 while a replay is in flight. */
s32 theaterIsReplaying(void);

/* -------------------------------------------------------------------------
 * Replay listing (UI side)
 * ------------------------------------------------------------------------- */

#define THEATER_REPLAY_LIST_MAX 32
#define THEATER_REPLAY_NAME_MAX 96

typedef struct theater_replay_entry_s {
	char filename[THEATER_REPLAY_NAME_MAX];
	u64  start_time_unix;
	u32  frame_count;
	u32  size_bytes;
} theater_replay_entry_t;

/**
 * Refresh the in-memory list of replay files in <home>/replays/.
 * Returns the entry count (0..THEATER_REPLAY_LIST_MAX). Subsequent
 * calls re-scan; the cache is invalidated on every refresh call.
 */
s32 theaterRefreshList(void);

/** Read entry at `idx` (0-based); NULL on OOB. */
const theater_replay_entry_t *theaterListAt(s32 idx);

#ifdef __cplusplus
}
#endif

#endif /* _IN_THEATER_H */
