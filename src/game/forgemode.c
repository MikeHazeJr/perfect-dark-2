/**
 * forgemode.c -- Forge level editor F0 Foundation.
 *
 * Implements the in-game forge session state machine and free-fly editor
 * camera.  See forgemode.h for the public API and the design ref at
 * context/designs/forge-level-editor-2026-04-16.md.
 *
 * F0 hijack pattern (kept deliberately minimal):
 *  - Entering FREEFLY snaps the freefly camera to the player's current
 *    pos+look and saves the original bondmovemode.
 *  - bondmovemode is set to MOVEMODE_CUTSCENE so bmoveTick routes to
 *    bcutsceneTick (no walk/look update from the legacy path).
 *  - Each tick, forgeTick reads input axes, integrates the freefly camera,
 *    then writes the result into g_Vars.currentplayer->prop->pos and
 *    vv_theta/vv_verta.  No collision; the camera is a ghost.
 *  - Exiting FREEFLY restores bondmovemode and leaves the player at the
 *    freefly position (drop-to-ground polish lands later).
 */

#include "game/forgemode.h"

#include <math.h>

#include "PR/ultratypes.h"

#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"

#include "system.h"

#include "actionmap.h"

/* ============================================================
 * Tunables
 * ============================================================ */

/* Per-second world-unit speeds.  PD coords: ~250u/s = walking pace. */
#define FORGE_FREEFLY_SPEED_DEFAULT   600.0f
#define FORGE_FREEFLY_SPEED_BOOST_X   3.0f
#define FORGE_FREEFLY_SPEED_PRECISE_X 0.25f

/* Look sensitivity multiplier for ACTION_AXIS_AIM_*.  Tuned to feel close
 * to the in-game look speed. */
#define FORGE_FREEFLY_LOOK_SENS_DEG   180.0f

/* Pitch clamp to avoid gimbal lock at vertical view. */
#define FORGE_FREEFLY_PITCH_MAX_DEG   89.0f

/* Frame dt assumed for F0 (60Hz tick). */
#define FORGE_FRAME_DT                (1.0f / 60.0f)

#define FORGE_DEG2RAD                 0.017453292519943f
#define FORGE_RAD2DEG                 57.295779513082320f

/* ============================================================
 * Module state
 * ============================================================ */

typedef struct forge_freefly_state {
	struct coord pos;        /* world-space camera position */
	f32 yaw_deg;             /* matches player vv_theta convention (degrees) */
	f32 pitch_deg;           /* matches vv_verta convention (degrees, +up) */
	f32 current_speed_scale; /* last-applied scale for HUD readout */
	s32 saved_movemode;      /* original bondmovemode at FREEFLY entry */
	bool has_saved_movemode;
} forge_freefly_state_t;

typedef struct forge_module {
	forge_session_state_t state;
	bool initialized;
	bool request_enter_session;
	forge_freefly_state_t fly;
} forge_module_t;

static forge_module_t s_forge;

/* ============================================================
 * Helpers
 * ============================================================ */

static struct player *forgeCurrentPlayer(void)
{
	if (g_Vars.currentplayer == NULL) {
		return NULL;
	}

	if (g_Vars.currentplayer->prop == NULL) {
		return NULL;
	}

	return g_Vars.currentplayer;
}

static bool forgeStageIsGameplay(void)
{
	return STAGE_IS_GAMEPLAY(g_StageNum) ? true : false;
}

static void forgeSnapFreeflyToPlayer(void)
{
	struct player *p = forgeCurrentPlayer();
	if (!p) {
		return;
	}

	s_forge.fly.pos = p->prop->pos;
	s_forge.fly.yaw_deg = p->vv_theta;
	s_forge.fly.pitch_deg = p->vv_verta;
	s_forge.fly.current_speed_scale = 1.0f;
}

static void forgeSetFreeflyMode(struct player *p)
{
	if (!s_forge.fly.has_saved_movemode) {
		s_forge.fly.saved_movemode = p->bondmovemode;
		s_forge.fly.has_saved_movemode = true;
	}
	p->bondmovemode = MOVEMODE_CUTSCENE;
}

static void forgeRestorePlayerMode(struct player *p)
{
	if (s_forge.fly.has_saved_movemode) {
		p->bondmovemode = (s8)s_forge.fly.saved_movemode;
		s_forge.fly.has_saved_movemode = false;
	}
}

static void forgeApplyFreeflyToPlayer(struct player *p)
{
	/* Hijack the player chr each tick so the existing camera matrix path
	 * (player.c -> camSetLookAt) renders from the freefly position+angle. */
	p->prop->pos.x = s_forge.fly.pos.x;
	p->prop->pos.y = s_forge.fly.pos.y;
	p->prop->pos.z = s_forge.fly.pos.z;
	p->vv_theta = s_forge.fly.yaw_deg;
	p->vv_verta = s_forge.fly.pitch_deg;
}

static void forgeReadFreeflyInput(f32 *out_move_x, f32 *out_move_y,
		f32 *out_aim_x, f32 *out_aim_y, f32 *out_vertical, f32 *out_speed_scale)
{
	f32 mx = 0.0f, my = 0.0f;
	f32 ax = 0.0f, ay = 0.0f;
	f32 vert = 0.0f;
	f32 scale = 1.0f;

	actionAxis(0, ACTION_AXIS_MOVE_X, &mx, &my);
	actionAxis(0, ACTION_AXIS_AIM_X, &ax, &ay);

	/* Digital fallback for the move axes when only WASD is bound to the
	 * directional digital actions (axis pair already covers stick + WASD,
	 * but defensively merge the digital values for a keyboard-only setup). */
	if (mx == 0.0f) {
		if (actionHeld(0, ACTION_MOVE_RIGHT)) mx += 1.0f;
		if (actionHeld(0, ACTION_MOVE_LEFT))  mx -= 1.0f;
	}
	if (my == 0.0f) {
		if (actionHeld(0, ACTION_MOVE_FORWARD))  my += 1.0f;
		if (actionHeld(0, ACTION_MOVE_BACKWARD)) my -= 1.0f;
	}

	if (actionHeld(0, ACTION_FORGE_ASCEND))  vert += 1.0f;
	if (actionHeld(0, ACTION_FORGE_DESCEND)) vert -= 1.0f;

	if (actionHeld(0, ACTION_FORGE_BOOST))     scale = FORGE_FREEFLY_SPEED_BOOST_X;
	if (actionHeld(0, ACTION_FORGE_PRECISION)) scale = FORGE_FREEFLY_SPEED_PRECISE_X;

	*out_move_x = mx;
	*out_move_y = my;
	*out_aim_x = ax;
	*out_aim_y = ay;
	*out_vertical = vert;
	*out_speed_scale = scale;
}

static void forgeUpdateFreefly(void)
{
	struct player *p = forgeCurrentPlayer();
	if (!p) {
		return;
	}

	f32 mx = 0.0f, my = 0.0f, ax = 0.0f, ay = 0.0f, vert = 0.0f, scale = 1.0f;
	forgeReadFreeflyInput(&mx, &my, &ax, &ay, &vert, &scale);

	/* Yaw / pitch from look axes.  Right stick X positive = look right (yaw
	 * increases CW from above, matching bwalk convention).  Right stick Y
	 * positive = look down on most controllers; invert so positive = up to
	 * match vv_verta semantics (positive = look up). */
	s_forge.fly.yaw_deg += ax * FORGE_FREEFLY_LOOK_SENS_DEG * FORGE_FRAME_DT;
	s_forge.fly.pitch_deg -= ay * FORGE_FREEFLY_LOOK_SENS_DEG * FORGE_FRAME_DT;

	/* Wrap yaw to avoid float overflow; clamp pitch to keep level look. */
	while (s_forge.fly.yaw_deg < 0.0f)    s_forge.fly.yaw_deg += 360.0f;
	while (s_forge.fly.yaw_deg >= 360.0f) s_forge.fly.yaw_deg -= 360.0f;
	if (s_forge.fly.pitch_deg >  FORGE_FREEFLY_PITCH_MAX_DEG) s_forge.fly.pitch_deg =  FORGE_FREEFLY_PITCH_MAX_DEG;
	if (s_forge.fly.pitch_deg < -FORGE_FREEFLY_PITCH_MAX_DEG) s_forge.fly.pitch_deg = -FORGE_FREEFLY_PITCH_MAX_DEG;

	/* Build the world-space movement vector.  PD yaw 0 looks down +Z and
	 * increases turning right (CW from above) -- this matches the existing
	 * bwalkUpdateTheta convention used by the legacy walk code. */
	const f32 yaw_rad   = s_forge.fly.yaw_deg   * FORGE_DEG2RAD;
	const f32 pitch_rad = s_forge.fly.pitch_deg * FORGE_DEG2RAD;
	const f32 cos_yaw   = cosf(yaw_rad);
	const f32 sin_yaw   = sinf(yaw_rad);
	const f32 cos_pitch = cosf(pitch_rad);
	const f32 sin_pitch = sinf(pitch_rad);

	/* Camera-relative forward (XZ horizontal projection scaled by pitch
	 * cos so pitch contributes Y, but we still treat WASD as 6DOF horizontal
	 * + Q/E vertical -- forward axis still gets vertical contribution from
	 * pitch for that "look down and fly down" feel). */
	const f32 fwd_x =  sin_yaw * cos_pitch;
	const f32 fwd_y =  sin_pitch;
	const f32 fwd_z =  cos_yaw * cos_pitch;

	/* Right vector (yaw + 90deg, no pitch). */
	const f32 right_x =  cos_yaw;
	const f32 right_y =  0.0f;
	const f32 right_z = -sin_yaw;

	const f32 speed = FORGE_FREEFLY_SPEED_DEFAULT * scale * FORGE_FRAME_DT;

	s_forge.fly.pos.x += (fwd_x * my + right_x * mx) * speed;
	s_forge.fly.pos.y += (fwd_y * my + right_y * mx) * speed;
	s_forge.fly.pos.z += (fwd_z * my + right_z * mx) * speed;

	/* Pure vertical from Q/E -- world-up so it stays intuitive when looking
	 * down. */
	s_forge.fly.pos.y += vert * speed;

	s_forge.fly.current_speed_scale = scale;

	forgeApplyFreeflyToPlayer(p);
}

/* ============================================================
 * State transitions
 * ============================================================ */

static void forgeTransitionToNormal(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (p) {
		forgeRestorePlayerMode(p);
	}
	if (s_forge.state != FORGE_SESSION_NORMAL) {
		sysLogPrintf(LOG_NOTE, "FORGE: -> NORMAL (%s)", reason ? reason : "");
	}
	s_forge.state = FORGE_SESSION_NORMAL;
}

static void forgeTransitionToFreefly(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (!p) {
		sysLogPrintf(LOG_WARNING, "FORGE: cannot enter FREEFLY -- no current player");
		return;
	}
	forgeSnapFreeflyToPlayer();
	forgeSetFreeflyMode(p);
	if (s_forge.state != FORGE_SESSION_FREEFLY) {
		sysLogPrintf(LOG_NOTE, "FORGE: -> FREEFLY (%s) pos=(%.0f,%.0f,%.0f) yaw=%.1f",
				reason ? reason : "",
				s_forge.fly.pos.x, s_forge.fly.pos.y, s_forge.fly.pos.z,
				s_forge.fly.yaw_deg);
	}
	s_forge.state = FORGE_SESSION_FREEFLY;
}

static void forgeTransitionToInactive(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (p) {
		forgeRestorePlayerMode(p);
	}
	if (s_forge.state != FORGE_SESSION_INACTIVE) {
		sysLogPrintf(LOG_NOTE, "FORGE: -> INACTIVE (%s)", reason ? reason : "");
	}
	s_forge.state = FORGE_SESSION_INACTIVE;
	s_forge.request_enter_session = false;
}

/* ============================================================
 * Public API
 * ============================================================ */

void forgeInit(void)
{
	if (s_forge.initialized) {
		return;
	}
	s_forge.state = FORGE_SESSION_INACTIVE;
	s_forge.request_enter_session = false;
	s_forge.fly.pos.x = 0.0f;
	s_forge.fly.pos.y = 0.0f;
	s_forge.fly.pos.z = 0.0f;
	s_forge.fly.yaw_deg = 0.0f;
	s_forge.fly.pitch_deg = 0.0f;
	s_forge.fly.current_speed_scale = 1.0f;
	s_forge.fly.saved_movemode = MOVEMODE_WALK;
	s_forge.fly.has_saved_movemode = false;
	s_forge.initialized = true;
	sysLogPrintf(LOG_NOTE, "FORGE: init");
}

void forgeRequestEnterSession(void)
{
	s_forge.request_enter_session = true;
	sysLogPrintf(LOG_NOTE, "FORGE: enter requested (will activate on gameplay stage load)");
}

void forgeExitSession(void)
{
	if (s_forge.state == FORGE_SESSION_INACTIVE) {
		s_forge.request_enter_session = false;
		return;
	}
	forgeTransitionToInactive("explicit exit");
}

forge_session_state_t forgeGetSessionState(void)
{
	return s_forge.state;
}

s32 forgeSessionIsActive(void)
{
	return (s_forge.state != FORGE_SESSION_INACTIVE) ? 1 : 0;
}

s32 forgeIsFreefly(void)
{
	return (s_forge.state == FORGE_SESSION_FREEFLY) ? 1 : 0;
}

void forgeGetCameraPos(struct coord *out_pos)
{
	if (!out_pos) {
		return;
	}
	*out_pos = s_forge.fly.pos;
}

f32 forgeGetCameraYawDeg(void)
{
	return s_forge.fly.yaw_deg;
}

f32 forgeGetCameraPitchDeg(void)
{
	return s_forge.fly.pitch_deg;
}

f32 forgeGetCurrentSpeedScale(void)
{
	return s_forge.fly.current_speed_scale;
}

/* ============================================================
 * Tick
 * ============================================================ */

void forgeTick(void)
{
	if (!s_forge.initialized) {
		forgeInit();
	}

	const bool stage_is_gameplay = forgeStageIsGameplay();

	/* Session-exit watchdog: any return to a system stage (title, credits,
	 * pak menu, 4MB) drops us out cleanly. */
	if (s_forge.state != FORGE_SESSION_INACTIVE && !stage_is_gameplay) {
		forgeTransitionToInactive("stage left gameplay");
		return;
	}

	/* Pending session-enter request: activate as soon as we're on a
	 * gameplay stage with a current player wired up. */
	if (s_forge.request_enter_session && stage_is_gameplay) {
		struct player *p = forgeCurrentPlayer();
		if (p) {
			s_forge.request_enter_session = false;
			forgeTransitionToNormal("session start (request)");
			sysLogPrintf(LOG_NOTE, "FORGE: session active stage=0x%02x", g_StageNum);
		}
	}

	if (s_forge.state == FORGE_SESSION_INACTIVE) {
		return;
	}

	/* In-session: handle the toggle binding (single tap). */
	if (actionPressed(0, ACTION_FORGE_TOGGLE)) {
		if (s_forge.state == FORGE_SESSION_NORMAL) {
			forgeTransitionToFreefly("toggle press");
		} else if (s_forge.state == FORGE_SESSION_FREEFLY) {
			forgeTransitionToNormal("toggle press");
		}
	}

	if (s_forge.state == FORGE_SESSION_FREEFLY) {
		forgeUpdateFreefly();
	}
}
