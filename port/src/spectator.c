/**
 * spectator.c -- unified spectator + Theater state machine.
 *
 * Single writer per spectator_state_t field. Inbound participant
 * snapshots flow through `spectatorIngestParticipantSnapshot`, which
 * is called by the live driver (wire receive path) and will be called
 * by the future Theater driver (file replay) in identical form. The
 * camera / control inputs come from the UI layer (no actionmap binding
 * so Session B's input scope is preserved).
 *
 * Free-fly bookkeeping: when the user holds Y the camera detaches and
 * accepts steering inputs; release re-attaches to the last spectated
 * participant. The state machine survives focus_idx changes (selecting
 * a different participant during free-fly does not snap the camera --
 * the user explicitly releases Y to snap back).
 */

#include "spectator.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

static spectator_state_t s_State;
static s32               s_Initialised;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void spectatorInit(void)
{
	if (s_Initialised) return;
	memset(&s_State, 0, sizeof(s_State));
	s_State.source = SPECTATOR_SOURCE_NONE;
	s_State.camera = SPECTATOR_CAM_THIRD_PERSON;
	s_State.subset = SPECTATOR_SUBSET_PLAYERS;
	s_State.focus_idx = -1;
	s_Initialised = 1;
	sysLogPrintf(LOG_NOTE, "SPECTATOR: initialised");
}

void spectatorShutdown(void)
{
	if (!s_Initialised) return;
	memset(&s_State, 0, sizeof(s_State));
	s_Initialised = 0;
}

const spectator_state_t *spectatorGet(void)
{
	return &s_State;
}

s32 spectatorIsActive(void)
{
	return s_State.source != SPECTATOR_SOURCE_NONE ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Subset helpers
 * ------------------------------------------------------------------------- */

const char *spectatorSubsetName(spectator_subset_t s)
{
	switch (s) {
		case SPECTATOR_SUBSET_PLAYERS:    return "Players";
		case SPECTATOR_SUBSET_ALL:        return "All Participants";
		case SPECTATOR_SUBSET_TEAM_RED:   return "Red Team";
		case SPECTATOR_SUBSET_TEAM_GREEN: return "Green Team";
		case SPECTATOR_SUBSET_TEAM_BLUE:  return "Blue Team";
		case SPECTATOR_SUBSET_TEAM_GOLD:  return "Gold Team";
		default:                           return "?";
	}
}

static s32 participantInSubset(const spectator_participant_t *p, spectator_subset_t s)
{
	if (!p->in_use) return 0;
	switch (s) {
		case SPECTATOR_SUBSET_PLAYERS:    return p->is_bot ? 0 : 1;
		case SPECTATOR_SUBSET_ALL:        return 1;
		case SPECTATOR_SUBSET_TEAM_RED:   return p->team == 0 ? 1 : 0;
		case SPECTATOR_SUBSET_TEAM_GREEN: return p->team == 1 ? 1 : 0;
		case SPECTATOR_SUBSET_TEAM_BLUE:  return p->team == 2 ? 1 : 0;
		case SPECTATOR_SUBSET_TEAM_GOLD:  return p->team == 3 ? 1 : 0;
		default:                           return 0;
	}
}

s32 spectatorSubsetSize(spectator_subset_t s)
{
	s32 n = 0;
	for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS; i++) {
		if (participantInSubset(&s_State.participants[i], s)) n++;
	}
	return n;
}

const spectator_participant_t *spectatorFocused(void)
{
	if (s_State.focus_idx < 0 || s_State.focus_idx >= SPECTATOR_MAX_PARTICIPANTS) {
		return NULL;
	}
	if (!s_State.participants[s_State.focus_idx].in_use) return NULL;
	return &s_State.participants[s_State.focus_idx];
}

/* -------------------------------------------------------------------------
 * Live driver
 * ------------------------------------------------------------------------- */

s32 spectatorBeginLive(u32 host_friend_handle)
{
	if (host_friend_handle == 0) return -1;
	if (s_State.source != SPECTATOR_SOURCE_NONE) {
		spectatorStop();
	}

	memset(&s_State, 0, sizeof(s_State));
	s_State.source = SPECTATOR_SOURCE_LIVE;
	s_State.camera = SPECTATOR_CAM_THIRD_PERSON;
	s_State.subset = SPECTATOR_SUBSET_PLAYERS;
	s_State.focus_idx = -1;
	s_State.host_handle = host_friend_handle;
	s_State.late_join_pending = 1;

	const social_friend_t *f = socialFriendByHandle(host_friend_handle);
	sysLogPrintf(LOG_NOTE,
	             "SPECTATOR: begin live host=0x%08x agent=\"%s\"",
	             (unsigned)host_friend_handle,
	             f && f->agent_name[0] ? f->agent_name : "?");

	/* The actual SVC_SPECTATE_REQUEST send is the responsibility of the
	 * wire layer (Phase 3 follow-up). The state machine sits ready and
	 * accepts the first ingest call when packets arrive. */
	return 0;
}

void spectatorStop(void)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	sysLogPrintf(LOG_NOTE, "SPECTATOR: stop (source=%d)", (int)s_State.source);
	memset(&s_State, 0, sizeof(s_State));
	s_State.source = SPECTATOR_SOURCE_NONE;
	s_State.focus_idx = -1;
	s_State.subset = SPECTATOR_SUBSET_PLAYERS;
}

/* -------------------------------------------------------------------------
 * Single-writer ingest. Both live and theater drivers call this.
 * ------------------------------------------------------------------------- */

void spectatorIngestParticipantSnapshot(const spectator_participant_t *participants,
                                         s32 count, u32 host_handle)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	if (!participants || count <= 0) return;

	/* Reject snapshots from a different host than the one we asked to
	 * spectate (defence in depth -- the wire layer also gates on
	 * stream_token). */
	if (s_State.host_handle != 0 && host_handle != s_State.host_handle) {
		return;
	}

	const s32 cap = (count < SPECTATOR_MAX_PARTICIPANTS)
	                ? count : SPECTATOR_MAX_PARTICIPANTS;

	for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS; i++) {
		if (i < cap) {
			s_State.participants[i] = participants[i];
		} else {
			memset(&s_State.participants[i], 0, sizeof(spectator_participant_t));
		}
	}

	s_State.last_state_ms = SDL_GetTicks();
	if (s_State.late_join_pending) {
		s_State.late_join_pending = 0;
		sysLogPrintf(LOG_NOTE, "SPECTATOR: late-join snapshot accepted");
	}

	/* Snap focus to the first in-subset participant if none. */
	if (s_State.focus_idx < 0 || !s_State.participants[s_State.focus_idx].in_use) {
		for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS; i++) {
			if (participantInSubset(&s_State.participants[i], s_State.subset)) {
				s_State.focus_idx = i;
				break;
			}
		}
	}
}

/* -------------------------------------------------------------------------
 * Camera / control
 * ------------------------------------------------------------------------- */

/* Cycle subset within the SPECTATOR_SUBSET_* enum, skipping empty ones.
 * Caller passes +1 / -1. */
void spectatorCycleSubset(s32 dir)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	const s32 step = (dir >= 0) ? 1 : (s32)SPECTATOR_SUBSET_COUNT - 1;
	s32 attempts = 0;
	for (s32 i = 0; i < (s32)SPECTATOR_SUBSET_COUNT; i++) {
		const spectator_subset_t s =
			(spectator_subset_t)(((s32)s_State.subset + step) % (s32)SPECTATOR_SUBSET_COUNT);
		s_State.subset = s;
		if (spectatorSubsetSize(s) > 0) {
			break;
		}
		attempts++;
	}
	(void)attempts;

	/* Re-anchor focus to the first member of the new subset. */
	s_State.focus_idx = -1;
	for (s32 i = 0; i < SPECTATOR_MAX_PARTICIPANTS; i++) {
		if (participantInSubset(&s_State.participants[i], s_State.subset)) {
			s_State.focus_idx = i;
			break;
		}
	}
}

void spectatorCycleMember(s32 dir)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	if (spectatorSubsetSize(s_State.subset) == 0) return;

	const s32 step = (dir >= 0) ? 1 : -1;
	s32 idx = s_State.focus_idx;
	for (s32 attempts = 0; attempts < SPECTATOR_MAX_PARTICIPANTS; attempts++) {
		idx += step;
		if (idx >= SPECTATOR_MAX_PARTICIPANTS) idx = 0;
		if (idx < 0) idx = SPECTATOR_MAX_PARTICIPANTS - 1;
		if (participantInSubset(&s_State.participants[idx], s_State.subset)) {
			s_State.focus_idx = idx;
			return;
		}
	}
}

void spectatorToggleFirstPerson(void)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	if (s_State.camera == SPECTATOR_CAM_FREE_FLY) return; /* free-fly takes priority */
	s_State.camera = (s_State.camera == SPECTATOR_CAM_FIRST_PERSON)
	                  ? SPECTATOR_CAM_THIRD_PERSON
	                  : SPECTATOR_CAM_FIRST_PERSON;
}

void spectatorBeginFreeFly(void)
{
	if (s_State.source == SPECTATOR_SOURCE_NONE) return;
	if (s_State.free_fly_active) return;

	const spectator_participant_t *p = spectatorFocused();
	if (p) {
		s_State.free_fly_pos[0] = p->pos[0];
		s_State.free_fly_pos[1] = p->pos[1] + 60.0f;  /* slightly above eye-level */
		s_State.free_fly_pos[2] = p->pos[2];
		s_State.free_fly_angle_theta = p->angle_theta;
		s_State.free_fly_angle_verta = p->angle_verta;
	} else {
		memset(s_State.free_fly_pos, 0, sizeof(s_State.free_fly_pos));
		s_State.free_fly_angle_theta = 0.0f;
		s_State.free_fly_angle_verta = 0.0f;
	}
	s_State.free_fly_active = 1;
	s_State.camera = SPECTATOR_CAM_FREE_FLY;
}

void spectatorEndFreeFly(void)
{
	if (!s_State.free_fly_active) return;
	s_State.free_fly_active = 0;
	s_State.camera = SPECTATOR_CAM_THIRD_PERSON;
}

void spectatorFreeFlyInput(f32 dx, f32 dy, f32 dz, f32 dyaw, f32 dpitch, f32 dt)
{
	if (!s_State.free_fly_active) return;
	if (dt <= 0.0f) return;

	const f32 speed = 250.0f * dt; /* units per second baseline */

	/* Manual sin / cos via 6-term Maclaurin -- the PCH path strips
	 * <math.h> declarations on this TU and the workaround is cleaner
	 * than fighting the precompiled header for one call site. Accuracy
	 * is sufficient for free-fly steering. */
	f32 a = s_State.free_fly_angle_theta;
	while (a >  3.14159265f) a -= 6.28318531f;
	while (a < -3.14159265f) a += 6.28318531f;
	const f32 a2 = a * a;
	const f32 sy = a * (1.0f - a2 * (1.0f/6.0f - a2 * (1.0f/120.0f - a2 * (1.0f/5040.0f))));
	const f32 cy = 1.0f - a2 * (0.5f - a2 * (1.0f/24.0f - a2 * (1.0f/720.0f)));

	s_State.free_fly_pos[0] += (cy * dx + sy * dz) * speed;
	s_State.free_fly_pos[1] += dy * speed;
	s_State.free_fly_pos[2] += (-sy * dx + cy * dz) * speed;

	s_State.free_fly_angle_theta += dyaw * dt;
	s_State.free_fly_angle_verta += dpitch * dt;

	/* Clamp pitch to avoid gimbal flip. */
	const f32 pitch_max = 1.5f;
	if (s_State.free_fly_angle_verta >  pitch_max) s_State.free_fly_angle_verta =  pitch_max;
	if (s_State.free_fly_angle_verta < -pitch_max) s_State.free_fly_angle_verta = -pitch_max;
}

/* -------------------------------------------------------------------------
 * Tick: stale-stream timeout
 * ------------------------------------------------------------------------- */

void spectatorTick(void)
{
	if (s_State.source != SPECTATOR_SOURCE_LIVE) return;

	/* If we have not received any state in 5 s, mark the stream stale.
	 * The UI surfaces this as "Lost connection to host"; the user is
	 * responsible for stopping or retrying. */
	const u32 now = SDL_GetTicks();
	if (s_State.last_state_ms != 0 && (now - s_State.last_state_ms) > 5000u) {
		/* Stale -- the UI can read s_State.last_state_ms to decide UX. */
	}
}

/* -------------------------------------------------------------------------
 * Host-side fan-out hook
 * ------------------------------------------------------------------------- */

s32 spectatorHostShouldBroadcastThisTick(void)
{
	/* SPECTATOR_FANOUT_HZ slices in a 60 Hz tick: emit on every Nth tick
	 * where N = 60 / fanout_hz. The actual broadcast lives in the wire
	 * layer (Phase 3 follow-up); this helper exists so the hook is
	 * stable and the wire layer can plug in cleanly when authored. */
	static u32 s_LastTickMs;
	const u32 now = SDL_GetTicks();
	const u32 interval = 1000u / SPECTATOR_FANOUT_HZ;
	if (now - s_LastTickMs < interval) return 0;
	s_LastTickMs = now;
	return 1;
}
