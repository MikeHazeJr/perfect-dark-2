/**
 * listening_room.h -- Phase 4 host-curated music-together channel.
 *
 * Per Q10 + design Section 6 ("Music-listening-together"):
 *
 *   - Session host owns a public playlist (a list of mod-music tracks).
 *   - Songs propagate to listeners via the EXISTING mod-distribution
 *     rails (port/src/file_transfer.c + the modmgr already in dev from
 *     Priority M). No new audio-streaming protocol.
 *   - Listeners cache tracks for the session; "Save permanently"
 *     promotes the track's mod folder into the user's permanent mod
 *     library.
 *   - Match track wins precedence over listening-room track when
 *     listeners are also in-match together (Issue 4a's match-scoped
 *     track lock from `b3d6e5f9`). Listening-room track resumes after
 *     the match ends.
 *
 * Phase 4 ships the host playlist + listener subscription + precedence
 * rule. The actual track-distribution wire ride uses the existing
 * SVC_DISTRIB_BEGIN / CHUNK / END pipeline (no new packets); the new
 * surface is the playlist manifest broadcast + the per-listener
 * subscribe/unsubscribe RPC.
 *
 * Wire integration (NET_PROTOCOL_VER 41 already includes
 * SVC_ACHIEVEMENT_TOAST; this module reuses SVC_MUSIC_ADVANCE for
 * track-change broadcasts). New playlist-manifest packets are scoped
 * to a Phase 4 wire follow-up; the data structures here ship now so
 * the UI surface is testable end-to-end on a single client.
 */

#ifndef _IN_LISTENING_ROOM_H
#define _IN_LISTENING_ROOM_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LR_MAX_TRACKS 32
#define LR_TRACK_ID_MAX 64
#define LR_TRACK_NAME_MAX 64

typedef enum {
	LR_STATE_OFF             = 0,  /* not in any listening room */
	LR_STATE_HOST            = 1,  /* I own the playlist */
	LR_STATE_LISTENER        = 2,  /* I am a listener */
	LR_STATE_MUTED_BY_MATCH  = 3,  /* match track took precedence */
} listening_room_state_t;

typedef struct lr_track_s {
	char track_id[LR_TRACK_ID_MAX];     /* mod-track catalog id */
	char display_name[LR_TRACK_NAME_MAX];
	u32  duration_ms;                   /* 0 if unknown */
	u32  has_local_copy;                /* 1 if listener has the mod cached */
} lr_track_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void listeningRoomInit(void);
void listeningRoomShutdown(void);
void listeningRoomTick(void);

/* -------------------------------------------------------------------------
 * State accessors
 * ------------------------------------------------------------------------- */

listening_room_state_t listeningRoomState(void);

/** Friend handle of the listening-room host (0 when host is local). */
u32 listeningRoomHostHandle(void);

s32  listeningRoomTrackCount(void);
const lr_track_t *listeningRoomTrackAt(s32 idx);

/** Index of the currently-playing track (-1 if no current track). */
s32 listeningRoomCurrentIdx(void);

/* -------------------------------------------------------------------------
 * Host operations
 * ------------------------------------------------------------------------- */

/** Become the host of a new listening room. Idempotent. */
s32 listeningRoomHostBegin(void);

/** Add a track to the host playlist. track_id is a mod-music catalog
 *  id; the mod must be installed locally on the host. Returns 0 on
 *  success, -1 if not host, list full, or track already present. */
s32 listeningRoomHostAddTrack(const char *track_id, const char *display_name);

/** Remove a track from the host playlist. Returns 0 on success. */
s32 listeningRoomHostRemoveTrack(const char *track_id);

/** Advance to a specific track (or the next track if idx < 0). */
s32 listeningRoomHostPlayTrack(s32 idx);

/* -------------------------------------------------------------------------
 * Listener operations
 * ------------------------------------------------------------------------- */

/**
 * Subscribe to a friend's listening room. Begins fetching missing
 * tracks via the existing mod-distribution / file-transfer rails.
 * Returns 0 on success.
 */
s32 listeningRoomSubscribe(u32 host_handle);

/** Unsubscribe + leave the listening room. */
void listeningRoomLeave(void);

/** Promote the current track to the user's permanent mod library
 *  (caches "Save permanently" per Q10). Returns 0 on success. */
s32 listeningRoomPromoteCurrentTrack(void);

/* -------------------------------------------------------------------------
 * Precedence (Issue 4a + Q10)
 *
 * The audio system queries this each tick: "should the listening-room
 * track be playing right now?" Returns 0 when the current match has
 * imposed its track lock; returns 1 when no match track is active.
 * ------------------------------------------------------------------------- */

s32 listeningRoomShouldPlay(void);

/** Match started -- mute the listening-room track until match ends. */
void listeningRoomOnMatchStart(void);

/** Match ended -- resume the listening-room track. */
void listeningRoomOnMatchEnd(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_LISTENING_ROOM_H */
