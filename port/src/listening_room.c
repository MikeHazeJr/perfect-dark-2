/**
 * listening_room.c -- host-curated music-together state.
 *
 * Single writer for s_State and s_Tracks: this module. Hosts mutate
 * via listeningRoomHost*; listeners mutate via listeningRoomSubscribe
 * / Leave / PromoteCurrentTrack. The match-vs-room precedence flag
 * has two write sites (OnMatchStart / OnMatchEnd) consumed by the
 * audio layer's listeningRoomShouldPlay() query.
 *
 * Track distribution piggybacks on the existing mod distribution
 * pipeline (port/src/file_transfer.c + modmgr from Priority M).  When
 * a listener subscribes, listeningRoomTick scans the playlist for
 * tracks where has_local_copy == 0 and queues a fetch via the
 * existing pipe.  This commit ships the data structures + the host /
 * listener API surface; the wire-side packet for "host announces
 * playlist" is a follow-up that piggybacks on the existing
 * distribution rails.
 */

#include "listening_room.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static listening_room_state_t s_State;
static u32                    s_HostHandle;       /* 0 when local is host */
static lr_track_t             s_Tracks[LR_MAX_TRACKS];
static s32                    s_NumTracks;
static s32                    s_CurrentIdx;
static u8                     s_MatchSuppress;    /* 1 when match imposes track lock */

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void listeningRoomInit(void)
{
	s_State = LR_STATE_OFF;
	s_HostHandle = 0;
	memset(s_Tracks, 0, sizeof(s_Tracks));
	s_NumTracks = 0;
	s_CurrentIdx = -1;
	s_MatchSuppress = 0;
}

void listeningRoomShutdown(void)
{
	listeningRoomLeave();
}

/* -------------------------------------------------------------------------
 * Read accessors
 * ------------------------------------------------------------------------- */

listening_room_state_t listeningRoomState(void)
{
	if (s_MatchSuppress && s_State != LR_STATE_OFF) return LR_STATE_MUTED_BY_MATCH;
	return s_State;
}

u32 listeningRoomHostHandle(void)
{
	return s_HostHandle;
}

s32 listeningRoomTrackCount(void) { return s_NumTracks; }

const lr_track_t *listeningRoomTrackAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumTracks) return NULL;
	return &s_Tracks[idx];
}

s32 listeningRoomCurrentIdx(void) { return s_CurrentIdx; }

/* -------------------------------------------------------------------------
 * Host operations
 * ------------------------------------------------------------------------- */

s32 listeningRoomHostBegin(void)
{
	if (s_State == LR_STATE_HOST) return 0;
	if (s_State == LR_STATE_LISTENER) listeningRoomLeave();

	s_State = LR_STATE_HOST;
	s_HostHandle = 0;     /* 0 == local */
	s_NumTracks = 0;
	s_CurrentIdx = -1;
	memset(s_Tracks, 0, sizeof(s_Tracks));
	sysLogPrintf(LOG_NOTE, "LISTEN: host begin");
	return 0;
}

static s32 findTrack(const char *track_id)
{
	if (!track_id || !*track_id) return -1;
	for (s32 i = 0; i < s_NumTracks; i++) {
		if (!strcmp(s_Tracks[i].track_id, track_id)) return i;
	}
	return -1;
}

s32 listeningRoomHostAddTrack(const char *track_id, const char *display_name)
{
	if (s_State != LR_STATE_HOST) return -1;
	if (!track_id || !*track_id) return -1;
	if (s_NumTracks >= LR_MAX_TRACKS) return -1;
	if (findTrack(track_id) >= 0) return -1;

	lr_track_t *t = &s_Tracks[s_NumTracks++];
	memset(t, 0, sizeof(*t));
	strncpy(t->track_id, track_id, LR_TRACK_ID_MAX - 1);
	if (display_name && *display_name) {
		strncpy(t->display_name, display_name, LR_TRACK_NAME_MAX - 1);
	} else {
		strncpy(t->display_name, track_id, LR_TRACK_NAME_MAX - 1);
	}
	t->has_local_copy = 1; /* host has the mod by definition */
	sysLogPrintf(LOG_NOTE, "LISTEN: host added track \"%s\"", t->display_name);
	return 0;
}

s32 listeningRoomHostRemoveTrack(const char *track_id)
{
	if (s_State != LR_STATE_HOST) return -1;
	const s32 i = findTrack(track_id);
	if (i < 0) return -1;
	memmove(&s_Tracks[i], &s_Tracks[i + 1],
	        (size_t)(s_NumTracks - i - 1) * sizeof(lr_track_t));
	s_NumTracks--;
	memset(&s_Tracks[s_NumTracks], 0, sizeof(lr_track_t));
	if (s_CurrentIdx >= s_NumTracks) s_CurrentIdx = s_NumTracks - 1;
	if (s_CurrentIdx == i) s_CurrentIdx = -1;
	return 0;
}

s32 listeningRoomHostPlayTrack(s32 idx)
{
	if (s_State != LR_STATE_HOST) return -1;
	if (s_NumTracks == 0) return -1;

	if (idx < 0) {
		/* Advance to next. */
		if (s_CurrentIdx < 0 || s_CurrentIdx >= s_NumTracks - 1) {
			s_CurrentIdx = 0;
		} else {
			s_CurrentIdx++;
		}
	} else {
		if (idx >= s_NumTracks) return -1;
		s_CurrentIdx = idx;
	}

	sysLogPrintf(LOG_NOTE, "LISTEN: host play idx=%d \"%s\"",
	             (int)s_CurrentIdx,
	             s_Tracks[s_CurrentIdx].display_name);
	return 0;
}

/* -------------------------------------------------------------------------
 * Listener operations
 * ------------------------------------------------------------------------- */

s32 listeningRoomSubscribe(u32 host_handle)
{
	if (host_handle == 0) return -1;
	if (host_handle == socialMyHandle()) return -1;
	if (!socialFriendByHandle(host_handle)) return -1;

	if (s_State == LR_STATE_HOST) {
		/* Hosting + listening simultaneously is intentionally disallowed
		 * to avoid playlist source ambiguity. The user explicitly stops
		 * hosting before subscribing. */
		return -1;
	}

	s_State = LR_STATE_LISTENER;
	s_HostHandle = host_handle;
	s_NumTracks = 0;
	s_CurrentIdx = -1;
	memset(s_Tracks, 0, sizeof(s_Tracks));
	/* Inbound playlist + track-change wire ride in a follow-up commit
	 * (uses the existing SVC_MUSIC_ADVANCE for the per-track
	 * advance, plus a new SVC_LISTEN_ROOM_PLAYLIST manifest packet
	 * documented in connectivity-phase1-decisions.md). */
	sysLogPrintf(LOG_NOTE, "LISTEN: listener subscribe host=0x%08x",
	             (unsigned)host_handle);
	return 0;
}

void listeningRoomLeave(void)
{
	if (s_State == LR_STATE_OFF) return;
	sysLogPrintf(LOG_NOTE, "LISTEN: leave (was state=%d)", (int)s_State);
	s_State = LR_STATE_OFF;
	s_HostHandle = 0;
	s_NumTracks = 0;
	s_CurrentIdx = -1;
	memset(s_Tracks, 0, sizeof(s_Tracks));
}

s32 listeningRoomPromoteCurrentTrack(void)
{
	if (s_State != LR_STATE_LISTENER) return -1;
	if (s_CurrentIdx < 0 || s_CurrentIdx >= s_NumTracks) return -1;
	const lr_track_t *t = &s_Tracks[s_CurrentIdx];
	if (!t->has_local_copy) return -1;
	/* Promotion is a mod-manager move: the track's parent mod folder
	 * gets renamed from the per-session cache to the permanent mods
	 * folder. The mod manager (from Priority M) exposes this via its
	 * "save shared" path; we log the intent here and the actual move
	 * lives there. */
	sysLogPrintf(LOG_NOTE,
	             "LISTEN: promote track \"%s\" to permanent library "
	             "(track_id=%s)",
	             t->display_name, t->track_id);
	return 0;
}

/* -------------------------------------------------------------------------
 * Match precedence
 * ------------------------------------------------------------------------- */

s32 listeningRoomShouldPlay(void)
{
	if (s_State == LR_STATE_OFF) return 0;
	if (s_MatchSuppress) return 0;
	if (s_CurrentIdx < 0) return 0;
	return 1;
}

void listeningRoomOnMatchStart(void)
{
	if (!s_MatchSuppress) {
		s_MatchSuppress = 1;
		if (s_State == LR_STATE_HOST || s_State == LR_STATE_LISTENER) {
			sysLogPrintf(LOG_NOTE,
			             "LISTEN: match started -- listening-room track suppressed");
		}
	}
}

void listeningRoomOnMatchEnd(void)
{
	if (s_MatchSuppress) {
		s_MatchSuppress = 0;
		if (s_State == LR_STATE_HOST || s_State == LR_STATE_LISTENER) {
			sysLogPrintf(LOG_NOTE,
			             "LISTEN: match ended -- listening-room track resumed");
		}
	}
}

/* -------------------------------------------------------------------------
 * Tick
 * ------------------------------------------------------------------------- */

void listeningRoomTick(void)
{
	if (s_State == LR_STATE_OFF) return;
	/* Listener side: future commit polls the host's playlist manifest +
	 * fetches missing tracks via file_transfer. The data structures are
	 * ready. */
}
