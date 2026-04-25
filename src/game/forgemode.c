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

#include "forge/forge_runtime.h"

#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"

#include "system.h"

#include "actionmap.h"
#include "game/env.h"
#include "game/music.h"
#include "game/sky.h"

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
	/* S310 R1: saved agent model so the visual body-swap back and
	 * forth between Dr. Carroll and the player's chosen character can
	 * happen without reloading the stage.  Applies the swap at the
	 * chr->bodynum / chr->headnum slots; full engine-level model
	 * hot-reload requires additional plumbing (bodyAllocateModel for
	 * the new pair) which is currently logged-only from here so the
	 * state transitions are auditable even before the visual swap
	 * lands. */
	s32 saved_bodynum;
	s32 saved_headnum;
	bool has_saved_body;
} forge_freefly_state_t;

#define FORGE_MAX_LOCAL_PLAYERS 4

typedef struct forge_module {
	forge_session_state_t state;                        /* global (legacy) */
	forge_session_state_t per_player[FORGE_MAX_LOCAL_PLAYERS];
	bool initialized;
	bool request_enter_session;
	forge_freefly_state_t fly;
	/* B-245 (2026-04-25): track the stagenum the session was started on so
	 * forgeTick can detect any unexpected stage transition away from it
	 * (e.g. Mission Failed -> CI hub, script-driven mainChangeToStage, etc.)
	 * and tear down cleanly. -1 = no session active. */
	s32 session_stagenum;
} forge_module_t;

static forge_module_t s_forge;

#ifndef BODY_DRCAROLL
/* Fallback constant so a header reshuffle doesn't silently break this file.
 * The real BODY_DRCAROLL is defined as 0x6b in src/include/constants.h. */
#  define BODY_DRCAROLL 0x6b
#endif

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

	/* B-247 (2026-04-24): validate p->prop->pos before copying. Mike's Grid
	 * log captured `pos=(0,-4294967040,0)` -- the Y component was the bit
	 * pattern 0xFFFFFF00 reinterpreted as float, a clear uninitialized-memory
	 * signature. The corrupt Y propagates through the rest of the freefly
	 * pipeline and the player falls through the level. Sane stage coords are
	 * O(thousands) of game units; anything beyond +-100000 is corrupt. Use
	 * a deterministic fallback (0,100,0) so the observer at least spawns at
	 * world origin above ground rather than at the bit-pattern coord. */
	const f32 SANITY_BOUND = 100000.0f;
	const struct coord src = p->prop->pos;
	bool x_ok = (src.x >= -SANITY_BOUND && src.x <= SANITY_BOUND);
	bool y_ok = (src.y >= -SANITY_BOUND && src.y <= SANITY_BOUND);
	bool z_ok = (src.z >= -SANITY_BOUND && src.z <= SANITY_BOUND);
	if (!x_ok || !y_ok || !z_ok) {
		sysLogPrintf(LOG_WARNING,
			"GRID: forgeSnapFreeflyToPlayer: invalid player pos=(%.0f,%.0f,%.0f) -- using fallback (0,100,0)",
			src.x, src.y, src.z);
		s_forge.fly.pos.x = 0.0f;
		s_forge.fly.pos.y = 100.0f;
		s_forge.fly.pos.z = 0.0f;
	} else {
		s_forge.fly.pos = src;
	}
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

	/* Priority D (2026-04-24): seamless observer body-swap.  Save the
	 * player chr's current bodynum/headnum so the normal character can
	 * be restored on exit, then write BODY_DRCAROLL + HEAD_RANDOM_GENDER
	 * onto the chr so third-person mirrors (and future co-op peers)
	 * render the observer as Dr. Carroll.  The legacy model-reload
	 * pipeline picks up the change on the next per-chr model tick --
	 * no stage reload and no explicit bodyAllocateModel call needed. */
	if (p->prop && p->prop->chr && !s_forge.fly.has_saved_body) {
		s_forge.fly.saved_bodynum = p->prop->chr->bodynum;
		s_forge.fly.saved_headnum = p->prop->chr->headnum;
		s_forge.fly.has_saved_body = true;
		p->prop->chr->bodynum = (u8)BODY_DRCAROLL;
		p->prop->chr->headnum = (u8)HEAD_RANDOM_GENDER;
		sysLogPrintf(LOG_NOTE,
				"GRID: freefly body-swap: save (body=0x%02x head=0x%02x) -> Dr. Carroll (0x%02x)",
				(u32)s_forge.fly.saved_bodynum,
				(u32)s_forge.fly.saved_headnum,
				(u32)BODY_DRCAROLL);
	}

	/* B-247 (2026-04-25): explicit pos init at the body-swap control point.
	 * forgeSnapFreeflyToPlayer (called immediately before this) already
	 * validated p->prop->pos and stored a known-good value in s_forge.fly.pos
	 * (falling back to (0,100,0) when the read was junk). Mirror that value
	 * back to p->prop->pos NOW so the chr is never at junk coords for even
	 * one tick -- without this, forgeApplyFreeflyToPlayer's first per-tick
	 * write happens on the next frame, leaving a 1-tick window where the
	 * observer chr could be at the original junk position. Belt-and-braces
	 * with the read-side guard in snap. */
	if (p->prop) {
		p->prop->pos.x = s_forge.fly.pos.x;
		p->prop->pos.y = s_forge.fly.pos.y;
		p->prop->pos.z = s_forge.fly.pos.z;
	}
}

static void forgeRestorePlayerMode(struct player *p)
{
	if (s_forge.fly.has_saved_movemode) {
		p->bondmovemode = (s8)s_forge.fly.saved_movemode;
		s_forge.fly.has_saved_movemode = false;
	}

	/* S310 R1 -- restore saved chr body/head.  The hot-swap here is
	 * seamless: no stage reload, the player chr just carries the
	 * bodynum/headnum pair again for any downstream code that checks
	 * (e.g. third-person mirrors, other players in co-op). */
	if (p->prop && p->prop->chr && s_forge.fly.has_saved_body) {
		p->prop->chr->bodynum = (u8)s_forge.fly.saved_bodynum;
		p->prop->chr->headnum = (u8)s_forge.fly.saved_headnum;
		s_forge.fly.has_saved_body = false;
		sysLogPrintf(LOG_NOTE,
				"GRID: normal body-restore: (body=0x%02x head=0x%02x) -- no stage reload",
				(u32)p->prop->chr->bodynum,
				(u32)p->prop->chr->headnum);
	}
}

static void forgeApplyFreeflyToPlayer(struct player *p)
{
	/* Hijack the player chr each tick so the existing camera matrix path
	 * (player.c -> camSetLookAt) renders from the freefly position+angle.
	 *
	 * B-247 (2026-04-25): write-side validation. forgeSnapFreeflyToPlayer
	 * already validates the read side at FREEFLY entry, but s_forge.fly.pos
	 * could still drift to NaN / excessive bounds via forgeUpdateFreefly
	 * accumulation (e.g. unbounded velocity from a stuck axis). Validate
	 * before writing to the player chr so a single bad frame doesn't corrupt
	 * the chr position permanently. Also reset s_forge.fly.pos to the
	 * fallback so subsequent ticks don't keep writing junk.
	 *
	 * B-248 (2026-04-25): gravity is naturally disabled for the observer
	 * because forgeSetFreeflyMode sets `bondmovemode = MOVEMODE_CUTSCENE`,
	 * which dispatches to bcutsceneTick (empty function). The chr's vertical
	 * position is therefore untouched by bondmove between forgeApplyFreeflyToPlayer
	 * calls. This unconditional write also acts as a per-tick "force-altitude"
	 * guard: if any path outside bondmove modifies pos.y (e.g. a setup-side
	 * physics tick), this overwrite snaps it back to the observer's intended
	 * altitude every frame. */
	const f32 SANITY_BOUND = 100000.0f;
	if (s_forge.fly.pos.x < -SANITY_BOUND || s_forge.fly.pos.x > SANITY_BOUND
			|| s_forge.fly.pos.y < -SANITY_BOUND || s_forge.fly.pos.y > SANITY_BOUND
			|| s_forge.fly.pos.z < -SANITY_BOUND || s_forge.fly.pos.z > SANITY_BOUND) {
		sysLogPrintf(LOG_WARNING,
			"GRID: forgeApplyFreeflyToPlayer: s_forge.fly.pos drifted out of bounds (%.0f,%.0f,%.0f) -- snapping to (0,100,0)",
			s_forge.fly.pos.x, s_forge.fly.pos.y, s_forge.fly.pos.z);
		s_forge.fly.pos.x = 0.0f;
		s_forge.fly.pos.y = 100.0f;
		s_forge.fly.pos.z = 0.0f;
	}
	p->prop->pos.x = s_forge.fly.pos.x;
	p->prop->pos.y = s_forge.fly.pos.y;
	p->prop->pos.z = s_forge.fly.pos.z;
	p->vv_theta = s_forge.fly.yaw_deg;
	p->vv_verta = s_forge.fly.pitch_deg;

	/* B-248 (2026-04-25): explicit gravity gate at the observer-control
	 * point. Natural disable via MOVEMODE_CUTSCENE -> bcutsceneTick (empty)
	 * is already in place, but zero the chr's fallspeed here so any
	 * chrTick path that would otherwise accumulate gravity for a frame
	 * cannot do so. Belt-and-braces with the bondmovemode override and
	 * the per-tick prop->pos overwrite above. */
	if (p->prop->chr) {
		p->prop->chr->fallspeed.x = 0.0f;
		p->prop->chr->fallspeed.y = 0.0f;
		p->prop->chr->fallspeed.z = 0.0f;
	}
}

static void forgeReadFreeflyInput(f32 *out_move_x, f32 *out_move_y,
		f32 *out_aim_x, f32 *out_aim_y, f32 *out_vertical, f32 *out_speed_scale)
{
	f32 mx = 0.0f, my = 0.0f;
	f32 ax = 0.0f, ay = 0.0f;
	f32 vert = 0.0f;
	f32 scale = 1.0f;

	/* Issue 8b invariant (2026-04-24): the freefly camera reads raw
	 * stick axes every tick regardless of sidebar visibility or Forge
	 * IMC state.
	 *
	 * Stick axes live on a path that bypasses IMC priority entirely --
	 * actionmapPollFrame in port/src/actionmap.cpp calls
	 * SDL_GameControllerGetAxis directly and writes to
	 * s_State[ACTION_AXIS_*].value.  actionAxis() returns those values
	 * gated only on gameplayInputSuppressed() (menu on top / focus lost
	 * / focus-settle window) -- not on forgeIsFreefly() and not on
	 * actionIsBlockedInFreefly().  ImGui never steals them either
	 * because ImGuiConfigFlags_NavEnableGamepad stays off
	 * (pdgui_backend.cpp B-124 fix).
	 *
	 * Net effect: toggling the editor sidebar with X, navigating it
	 * with D-pad, or cycling tabs with LB/RB does not consume stick
	 * input. The observer keeps full analog fly control at all times. */
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
		sysLogPrintf(LOG_NOTE, "GRID: -> NORMAL (%s)", reason ? reason : "");
		forgeRuntimeEnterPlay();
	}
	s_forge.state = FORGE_SESSION_NORMAL;
	/* B-245: capture session stagenum on first activation. */
	if (s_forge.session_stagenum < 0) {
		s_forge.session_stagenum = (s32)g_StageNum;
	}

	/* Issue 8b (2026-04-24): deactivate the Forge IMC when leaving
	 * FREEFLY so X / LB / RB / D-pad revert to their gameplay actions
	 * (USE / WEAPON_PREV / WEAPON_NEXT / FIRE_MODE). Safe to call when
	 * already inactive -- imcDeactivate no-ops in that case. */
	imcDeactivate(&g_ImcForge);
}

static void forgeTransitionToFreefly(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (!p) {
		sysLogPrintf(LOG_WARNING, "GRID: cannot enter FREEFLY -- no current player");
		return;
	}
	forgeSnapFreeflyToPlayer();
	forgeSetFreeflyMode(p);
	if (s_forge.state != FORGE_SESSION_FREEFLY) {
		sysLogPrintf(LOG_NOTE, "GRID: -> FREEFLY (%s) pos=(%.0f,%.0f,%.0f) yaw=%.1f",
				reason ? reason : "",
				s_forge.fly.pos.x, s_forge.fly.pos.y, s_forge.fly.pos.z,
				s_forge.fly.yaw_deg);
		forgeRuntimeExitPlay();
	}
	s_forge.state = FORGE_SESSION_FREEFLY;
	/* B-245: capture session stagenum on first activation. */
	if (s_forge.session_stagenum < 0) {
		s_forge.session_stagenum = (s32)g_StageNum;
	}

	/* Issue 8b (2026-04-24): activate the Forge IMC. Priority 7 is
	 * above gameplay (0) and vehicle (5), so the forge bindings win
	 * over any conflicting gameplay binding on the same VK for the
	 * duration of FREEFLY. Stick axes are unaffected -- they write
	 * directly to s_State via actionmapPollFrame, bypassing IMC
	 * priority entirely, so the freefly camera keeps full stick
	 * control even with the Forge IMC active. */
	imcActivate(&g_ImcForge);
}

static void forgeTransitionToInactive(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (p) {
		forgeRestorePlayerMode(p);
	}

	/* Defensive hardening (2026-04-24): forgeRestorePlayerMode is guarded
	 * on currentplayer being non-NULL AND p->prop && p->prop->chr being
	 * non-NULL. If the inactive transition fires while the player chr is
	 * already torn down (typical when stage transitions to TITLE / main
	 * menu), one or both of those guards skips the restore and the
	 * has_saved_* flags stay true with stale saved values. The chr being
	 * gone means the dangling state never lands on a live chr, but a
	 * future code path that reads saved_bodynum / saved_headnum without
	 * also re-checking validity would observe stale data.
	 *
	 * Belt-and-braces: unconditionally clear both has_saved_* flags so
	 * the inactive transition is idempotent regardless of currentplayer
	 * state. The integer saved_* fields are irrelevant once the flags
	 * are false (every consumer guards on the flag); leaving their
	 * last-known values keeps the diff minimal. */
	s_forge.fly.has_saved_movemode = false;
	s_forge.fly.has_saved_body     = false;

	if (s_forge.state != FORGE_SESSION_INACTIVE) {
		sysLogPrintf(LOG_NOTE, "GRID: -> INACTIVE (%s)", reason ? reason : "");
		forgeRuntimeExitPlay();
	}
	s_forge.state = FORGE_SESSION_INACTIVE;
	s_forge.request_enter_session = false;
	s_forge.session_stagenum = -1;  /* B-245: clear so next session captures fresh stagenum */

	/* Issue 8b + AUDIT-24-H2/H3: ensure both Forge IMCs are released
	 * when the session ends through any path (stage-left-gameplay
	 * watchdog, explicit exit, transition to NORMAL is handled
	 * separately). g_ImcForgeSession (FORGE_TOGGLE on Back) is whole-
	 * session scope; g_ImcForge (camera axes / sidebar / tabs) is
	 * FREEFLY scope. Both are idempotent on already-inactive. */
	imcDeactivate(&g_ImcForge);
	imcDeactivate(&g_ImcForgeSession);

	/* Cleanup (2026-04-24): the Playtest HUD's Freeze All toggle mirrors
	 * bs->all_frozen into g_BotUpdatesDisabled every forgeRuntimeTick.
	 * When the session ends (stage left gameplay, explicit exit, etc.)
	 * forgeRuntimeTick early-returns on !s_active, which leaves the flag
	 * at its last value.  If the user had Freeze All on and exited, the
	 * next non-Grid match inherits frozen bots.  Clear the flag on
	 * session exit so the next match starts clean.  Extern declared
	 * locally since there's no public header for g_BotUpdatesDisabled. */
	{
		extern s32 g_BotUpdatesDisabled;
		g_BotUpdatesDisabled = 0;
	}
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
	s_forge.session_stagenum = -1;  /* B-245: no session active at init */
	s_forge.fly.pos.x = 0.0f;
	s_forge.fly.pos.y = 0.0f;
	s_forge.fly.pos.z = 0.0f;
	s_forge.fly.yaw_deg = 0.0f;
	s_forge.fly.pitch_deg = 0.0f;
	s_forge.fly.current_speed_scale = 1.0f;
	s_forge.fly.saved_movemode = MOVEMODE_WALK;
	s_forge.fly.has_saved_movemode = false;
	s_forge.fly.saved_bodynum = 0;
	s_forge.fly.saved_headnum = 0;
	s_forge.fly.has_saved_body = false;
	for (s32 i = 0; i < FORGE_MAX_LOCAL_PLAYERS; ++i) {
		s_forge.per_player[i] = FORGE_SESSION_INACTIVE;
	}
	s_forge.initialized = true;
	sysLogPrintf(LOG_NOTE, "GRID: init");
}

void forgeRequestEnterSession(void)
{
	s_forge.request_enter_session = true;
	sysLogPrintf(LOG_NOTE, "GRID: enter requested (will activate on gameplay stage load)");
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
 * Per-player API (S310 R1)
 * ============================================================ */

forge_session_state_t forgeGetPlayerSessionState(s32 playerNum)
{
	if (playerNum < 0 || playerNum >= FORGE_MAX_LOCAL_PLAYERS) {
		return FORGE_SESSION_INACTIVE;
	}
	/* F0: single local player -- all queries proxy to the global state
	 * for player 0.  The per_player[] array is the wire for MP co-op
	 * forge (F8 stretch). */
	if (playerNum == 0) return s_forge.state;
	return s_forge.per_player[playerNum];
}

void forgeTogglePlayerMode(s32 playerNum)
{
	if (playerNum != 0) {
		/* MP co-op forge (F8) will consume per-player toggles through
		 * this entry point.  For now log and no-op for non-zero. */
		sysLogPrintf(LOG_NOTE,
				"GRID: toggle request for playerNum=%d deferred (MP co-op forge F8)",
				playerNum);
		return;
	}

	if (s_forge.state == FORGE_SESSION_NORMAL) {
		forgeTransitionToFreefly("seamless toggle (no stage reload)");
	} else if (s_forge.state == FORGE_SESSION_FREEFLY) {
		forgeTransitionToNormal("seamless toggle (no stage reload)");
	}
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

	/* B-245 (2026-04-25): Mission-Failed -> CI Restart and other unexpected
	 * stage transitions can move the player to a *different* gameplay stage
	 * than the one the Grid session was started on. STAGE_CITRAINING is a
	 * gameplay stage per `STAGE_IS_GAMEPLAY` (it is not in the system-stage
	 * trio of TITLE/BOOTPAKMENU/CREDITS), so the watchdog above does not
	 * fire and Grid session state survives the transition -- which leaves
	 * the Grid UI accessible from CI hub and breaks subsequent gameplay.
	 *
	 * Capture the session-launch stagenum on first transition out of
	 * INACTIVE (forgeTransitionToNormal / forgeTransitionToFreefly), and
	 * fire transitionToInactive whenever g_StageNum diverges from it. The
	 * Halo-style Forge<->Playtest in-place toggle keeps stagenum constant
	 * by design, so this watchdog only fires on actual stage transitions. */
	if (s_forge.state != FORGE_SESSION_INACTIVE
			&& s_forge.session_stagenum >= 0
			&& s_forge.session_stagenum != (s32)g_StageNum) {
		sysLogPrintf(LOG_NOTE,
			"GRID: stagenum diverged %d -> %d, tearing down session",
			(s32)s_forge.session_stagenum, (s32)g_StageNum);
		forgeTransitionToInactive("session stagenum diverged");
		return;
	}

	/* Pending session-enter request: activate as soon as we're on a
	 * gameplay stage with a current player wired up.
	 *
	 * P2 (2026-04-24): start the session in FREEFLY (Forge / edit) mode
	 * so the user lands in the editor by default.  The previous
	 * behaviour was to start in NORMAL (Playtest / inhabit) and require
	 * a manual F7 toggle.  The Halo-style Back-button swap still drops
	 * between Forge and Playtest in-place; this change just flips the
	 * default entry mode.  forgeTransitionToFreefly snaps the camera to
	 * the player's current pos / yaw / pitch, so there is no visual
	 * jump on the first frame. */
	if (s_forge.request_enter_session && stage_is_gameplay) {
		struct player *p = forgeCurrentPlayer();
		if (p) {
			s_forge.request_enter_session = false;
			/* AUDIT-24-H2: activate the whole-session IMC BEFORE the
			 * FREEFLY transition. This puts JBTN_BACK -> FORGE_TOGGLE
			 * on the wire as soon as the session is live, so the
			 * Halo-style toggle works in both directions (Back during
			 * Playtest enters FREEFLY; Back during FREEFLY returns to
			 * Playtest). The FREEFLY-scoped g_ImcForge piggybacks on
			 * this and is layered on top inside forgeTransitionToFreefly. */
			imcActivate(&g_ImcForgeSession);
			forgeTransitionToFreefly("session start (request)");
			sysLogPrintf(LOG_NOTE, "GRID: session active stage=0x%02x (starting in FREEFLY)",
					g_StageNum);
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

/* ============================================================
 * Level-tab accessors (Issue 9 execution, 2026-04-24)
 *
 * C wrappers for the forge editor's Level tab.  The editor is in a C++
 * TU that cannot include types.h (bool/s32 conflict), so reading or
 * writing struct environment fields goes through these opaque getters /
 * setters.  All writes take effect on the live env -- the skybox and
 * lighting update on the next tick, music starts immediately.
 * ============================================================ */

void forgeLevelGetSkyColor(u8 *out_r, u8 *out_g, u8 *out_b)
{
	struct environment *e = envGetCurrent();
	if (!e || !out_r || !out_g || !out_b) {
		return;
	}
	*out_r = e->sky_r;
	*out_g = e->sky_g;
	*out_b = e->sky_b;
}

void forgeLevelSetSkyColor(u8 r, u8 g, u8 b)
{
	struct environment *e = envGetCurrent();
	if (!e) {
		return;
	}
	e->sky_r = r;
	e->sky_g = g;
	e->sky_b = b;
}

void forgeLevelGetFog(s32 *out_fogmin, s32 *out_fogmax)
{
	struct environment *e = envGetCurrent();
	if (!e || !out_fogmin || !out_fogmax) {
		return;
	}
	*out_fogmin = e->fogmin;
	*out_fogmax = e->fogmax;
}

void forgeLevelSetFog(s32 fogmin, s32 fogmax)
{
	struct environment *e = envGetCurrent();
	if (!e) {
		return;
	}
	if (fogmin < 0) fogmin = 0;
	if (fogmax < fogmin) fogmax = fogmin + 1;
	e->fogmin = fogmin;
	e->fogmax = fogmax;
}

s32 forgeLevelGetCloudsEnabled(void)
{
	struct environment *e = envGetCurrent();
	return (e && e->clouds_enabled) ? 1 : 0;
}

void forgeLevelSetCloudsEnabled(s32 enabled)
{
	struct environment *e = envGetCurrent();
	if (!e) {
		return;
	}
	e->clouds_enabled = enabled ? 1 : 0;
}

void forgeLevelGetCloudColor(f32 *out_r, f32 *out_g, f32 *out_b)
{
	struct environment *e = envGetCurrent();
	if (!e || !out_r || !out_g || !out_b) {
		return;
	}
	*out_r = e->clouds_r;
	*out_g = e->clouds_g;
	*out_b = e->clouds_b;
}

void forgeLevelSetCloudColor(f32 r, f32 g, f32 b)
{
	struct environment *e = envGetCurrent();
	if (!e) {
		return;
	}
	e->clouds_r = r;
	e->clouds_g = g;
	e->clouds_b = b;
}

/* Swap the skybox to a different stage's sky.  The env pipeline resolves
 * this on the next envTick via envChooseAndApply; we also re-run
 * skyReset to refresh sun/artifact state immediately. */
void forgeLevelSetSkyStage(s32 stagenum)
{
	if (stagenum <= 0) {
		return;
	}
	envChooseAndApply(stagenum, false);
	skyReset((u32)stagenum);
}

/* Start a different primary music track immediately.  Non-destructive:
 * the original stage theme resumes on the next musicReset. */
void forgeLevelPlayMusic(s32 tracknum)
{
	if (tracknum <= 0) {
		return;
	}
	musicStartTemporaryPrimary(tracknum);
}

void forgeLevelStopMusic(void)
{
	musicStop();
}
