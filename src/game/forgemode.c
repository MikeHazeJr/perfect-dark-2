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
#include "inputctx.h"  /* Phase 2 fix #9: g_CtxForgeEditor push / pop on FREEFLY transitions */
#include "pdgui.h"  /* B-195 / Fix 2+3: pdguiClearImGuiFocusAndNav on transitions */
#include "scene.h"
#include "game/body.h"  /* Fix 5: bodyAllocateModel for chr-body hot-reload at transitions */
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
	/* Fix 5 (2026-05-01): chr-body hot-reload at transition time.  Both
	 * directions (FREEFLY <-> NORMAL) now allocate a fresh body+head
	 * model via bodyAllocateModel and bind it to chr->model so the
	 * Dr. Carroll body is actually present in freefly and the player's
	 * Agent body is restored on play-mode entry.  saved_chrmodel is the
	 * chr->model pointer captured at FREEFLY entry; we restore that exact
	 * pointer on exit so the gunmem / modelmgr lifecycle that originally
	 * allocated it stays consistent.  The Dr. Carroll model we allocate
	 * during FREEFLY is freed on the next stage unload (gunmem-pool free
	 * or modelmgr reset, whichever owns it); a per-transition explicit
	 * release would require deeper plumbing into the slot manager and is
	 * deferred -- leak per session is bounded (one alloc per FREEFLY
	 * entry; typical session has 1-2 transitions). */
	s32 saved_bodynum;
	s32 saved_headnum;
	bool has_saved_body;
	struct model *saved_chrmodel;
	bool has_saved_chrmodel;
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
	/* B-254 (2026-04-25): canvas mode flag.  When true, the active session
	 * was launched on a SP campaign stage as a build canvas; setup-time
	 * chr / AI / script paths consult forgeIsCanvasMode() and short-circuit
	 * to keep the stage's authored mission state from running.  Cleared
	 * when the session transitions to INACTIVE. */
	bool canvas_mode;
	/* B-254 (2026-04-25): mirror of canvas_mode at request time, before the
	 * session has activated (request_enter_session is true but state is
	 * still INACTIVE).  Latched into canvas_mode at activation in
	 * forgeTick. */
	bool request_canvas_mode;
	bool observer_layer_active;
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

	/* Fix 5 (2026-05-01): chr-body hot-reload at FREEFLY entry.
	 *
	 * Prior code only twiddled chr->bodynum / chr->headnum integer fields
	 * and trusted "the legacy model-reload pipeline picks up the change
	 * on the next per-chr model tick" -- empirically false. The 2026-05-01
	 * Grid playtest log shows haschrbody=0 throughout a freefly session
	 * because chr->model stayed at its allocation-time value (NULL when
	 * the player's chr was created on Grid stage entry). Result: Dr.
	 * Carroll never appears in third-person; the chr is bodyless.
	 *
	 * Fix: explicitly allocate a body+head model via bodyAllocateModel
	 * for the BODY_DRCAROLL / HEAD_RANDOM_GENDER pair and assign it to
	 * chr->model. Save the prior chr->model pointer so forgeRestorePlayerMode
	 * can restore the exact original allocation (which the gunmem / modelmgr
	 * pipeline still owns). The Dr Carroll model we allocate here leaks
	 * until the next stage unload (gunmem-pool free or modelmgr reset);
	 * per-transition explicit release would require deeper plumbing into
	 * the slot manager and is deferred. The leak is bounded -- one alloc
	 * per FREEFLY entry, ~1-2 entries per typical authoring session. */
	if (p->prop && p->prop->chr && !s_forge.fly.has_saved_body) {
		struct chrdata *chr = p->prop->chr;
		s_forge.fly.saved_bodynum   = chr->bodynum;
		s_forge.fly.saved_headnum   = chr->headnum;
		s_forge.fly.saved_chrmodel  = chr->model;
		s_forge.fly.has_saved_body     = true;
		s_forge.fly.has_saved_chrmodel = true;

		/* Mutate the integer fields first so any consumer that reads
		 * chr->bodynum during the bodyAllocateModel call (e.g. catalog
		 * resolve through manifestEnsureLoaded) sees the new identity. */
		chr->bodynum = (u8)BODY_DRCAROLL;
		chr->headnum = (u8)HEAD_RANDOM_GENDER;

		struct model *newmodel = bodyAllocateModel(BODY_DRCAROLL,
				HEAD_RANDOM_GENDER, 0);
		if (newmodel) {
			chr->model = newmodel;
			if (p == g_Vars.players[0]) {
				p->haschrbody = true;
				p->model00d4  = newmodel;
			}
			sysLogPrintf(LOG_NOTE,
					"GRID: freefly body-swap: save (body=0x%02x head=0x%02x model=%p) -> Dr. Carroll (0x%02x) newmodel=%p",
					(u32)s_forge.fly.saved_bodynum,
					(u32)s_forge.fly.saved_headnum,
					(void *)s_forge.fly.saved_chrmodel,
					(u32)BODY_DRCAROLL,
					(void *)newmodel);
		} else {
			/* Allocation failed -- leave chr->model alone (don't NULL it,
			 * gameplay paths assume non-NULL once chr is allocated) and
			 * log loud so any future regression surfaces here instead of
			 * silently rendering a bodyless chr. */
			sysLogPrintf(LOG_WARNING,
					"GRID: bodyAllocateModel returned NULL for Dr. Carroll (body=0x%02x head=0x%02x) -- chr body unchanged, freefly observer will be invisible",
					(u32)BODY_DRCAROLL, (u32)HEAD_RANDOM_GENDER);
		}
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

	/* Fix 5 (2026-05-01): chr-body restore at NORMAL entry.
	 *
	 * Two-part restore:
	 *   1. Restore the saved chr->model pointer so the gunmem / modelmgr
	 *      slot that originally backed the player's Agent body is back
	 *      in place. We do NOT free the Dr. Carroll model we allocated
	 *      at FREEFLY entry -- per-transition release needs deeper slot-
	 *      manager plumbing; the model leaks until the next stage unload
	 *      (acceptable per-session bound).
	 *   2. Restore the saved bodynum / headnum integer fields so any
	 *      downstream consumer reading the chr's identity sees Agent
	 *      again (third-person mirrors, future co-op peers, AI logic).
	 *
	 * If the saved model pointer was NULL at FREEFLY entry (e.g. forge
	 * intercepted before playerTickChrBody had populated it on stage
	 * load), restoring NULL would render the chr bodyless. Detect that
	 * case and allocate a fresh Agent body model via bodyAllocateModel
	 * so the chr ends up with a valid model regardless. */
	if (p->prop && p->prop->chr && s_forge.fly.has_saved_body) {
		struct chrdata *chr = p->prop->chr;
		const s32 restored_body = s_forge.fly.saved_bodynum;
		const s32 restored_head = s_forge.fly.saved_headnum;

		chr->bodynum = (u8)restored_body;
		chr->headnum = (u8)restored_head;
		s_forge.fly.has_saved_body = false;

		struct model *target_model = NULL;
		const char *path = "saved-pointer";

		if (s_forge.fly.has_saved_chrmodel && s_forge.fly.saved_chrmodel) {
			target_model = s_forge.fly.saved_chrmodel;
		} else {
			/* No saved Agent model (FREEFLY captured a NULL chr->model).
			 * Allocate a fresh one so the chr renders with the player's
			 * intended Agent identity instead of remaining bodyless. */
			target_model = bodyAllocateModel(restored_body, restored_head, 0);
			path = target_model ? "fresh-alloc" : "fresh-alloc-failed";
		}

		s_forge.fly.has_saved_chrmodel = false;
		s_forge.fly.saved_chrmodel     = NULL;

		if (target_model) {
			chr->model = target_model;
			if (p == g_Vars.players[0]) {
				p->haschrbody = true;
				p->model00d4  = target_model;
			}
		}

		sysLogPrintf(LOG_NOTE,
				"GRID: normal body-restore: (body=0x%02x head=0x%02x) model=%p path=%s",
				(u32)chr->bodynum, (u32)chr->headnum,
				(void *)chr->model, path);
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

	/* B-255 (2026-04-25): one-shot trace on first non-trivial move input
	 * so Mike can verify in playtest that the look-relative transform
	 * is reaching the position update. Logs the current yaw/pitch and
	 * the computed forward / right vectors. Fires once per session
	 * activation; cleared in forgeTransitionToInactive (canvas_mode
	 * field reset there also serves as the "session is starting fresh"
	 * latch for this diagnostic). If the math were upstream-broken
	 * (raw stick to world axes) the fwd vector would not change as
	 * the user yaw-rotates the camera. */
	{
		static bool s_b255_logged = false;
		if (!s_b255_logged && (mx != 0.0f || my != 0.0f)) {
			sysLogPrintf(LOG_NOTE,
				"GRID.B255: freefly move-frame trace yaw=%.1f pitch=%.1f "
				"fwd=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f) stick=(mx=%.2f,my=%.2f)",
				s_forge.fly.yaw_deg, s_forge.fly.pitch_deg,
				fwd_x, fwd_y, fwd_z, right_x, right_y, right_z, mx, my);
			s_b255_logged = true;
		}
	}

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

/* B-253 follow-up: auto-toggle the renderer's debug wireframe overlay
 * when entering Grid (any non-INACTIVE state) and restore on exit.
 * Per Mike: in The Grid the freefly camera flies through walls; an
 * editor-style wireframe view helps see geometry edges of distant
 * rooms.  Cull mode is left at the user's setting (default none).
 *
 * Save/restore is idempotent: if no save is pending we save current
 * state on first entry; subsequent entries (NORMAL <-> FREEFLY hops)
 * leave the saved state alone.  Exit restores once and clears.
 *
 * The renderer-debug API is in port/fast3d/gfx_opengl.cpp; declared
 * here as plain externs to avoid pulling that header into game code. */
extern int  gfxDebugCullModeGet(void);
extern void gfxDebugCullModeSet(int mode);
extern int  gfxDebugWireframeGet(void);
extern void gfxDebugWireframeSet(int on);

static int s_GridSavedCullMode  = -1;  /* -1 = no save pending */
static int s_GridSavedWireframe = -1;

static void forgeApplyDebugRenderEntry(void)
{
	if (s_GridSavedCullMode >= 0) {
		return;  /* already saved on a prior entry; skip */
	}
	s_GridSavedCullMode  = gfxDebugCullModeGet();
	s_GridSavedWireframe = gfxDebugWireframeGet();

	/* Auto-enable wireframe overlay so geometry edges read clearly
	 * while the freefly camera is positioned outside rooms. User can
	 * still override mid-session with Shift+F2. */
	gfxDebugWireframeSet(1);
}

static void forgeApplyDebugRenderExit(void)
{
	if (s_GridSavedCullMode < 0) {
		return;  /* no save pending */
	}
	gfxDebugCullModeSet(s_GridSavedCullMode);
	gfxDebugWireframeSet(s_GridSavedWireframe);
	s_GridSavedCullMode  = -1;
	s_GridSavedWireframe = -1;
}

static void forgeObserverLayerEnter(const char *reason)
{
	if (s_forge.observer_layer_active) {
		return;
	}

	SceneObserverPayload payload;
	payload.source = SCENE_OBSERVER_SOURCE_FORGE;

	if (sceneFire(SCENE_EVENT_OBSERVER_ENTER, &payload) == 0) {
		s_forge.observer_layer_active = true;
	} else {
		sysLogPrintf(LOG_WARNING,
			"GRID: observer layer enter rejected (%s)",
			reason ? reason : "");
	}
}

static void forgeObserverLayerExit(const char *reason)
{
	if (!s_forge.observer_layer_active) {
		return;
	}

	SceneObserverPayload payload;
	payload.source = SCENE_OBSERVER_SOURCE_FORGE;
	(void)reason;
	sceneFire(SCENE_EVENT_OBSERVER_EXIT, &payload);
	s_forge.observer_layer_active = false;
}

static void forgeTransitionToNormal(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (p) {
		forgeRestorePlayerMode(p);
	}
	forgeObserverLayerEnter(reason);
	if (s_forge.state != FORGE_SESSION_NORMAL) {
		sysLogPrintf(LOG_NOTE, "GRID: -> NORMAL (%s)", reason ? reason : "");
		forgeRuntimeEnterPlay();
	}
	s_forge.state = FORGE_SESSION_NORMAL;

	/* B-246 round-6 instrumentation: Forge sessions toggle the FP rig on
	 * and off as the player flips between FREEFLY observer and Playtest.
	 * Logging the gunctrl snapshot at each transition makes any FP-rig
	 * regression on session re-entry visible. */
	if (p && p == g_Vars.players[0]) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: forgeTransitionToNormal player=0 reason='%s' wpn=%d switchto=%d gunmemowner=%d masterload=%d handmodeldef=%p haschrbody=%d",
			reason ? reason : "",
			(s32)p->gunctrl.weaponnum,
			(s32)p->gunctrl.switchtoweaponnum,
			(s32)p->gunctrl.gunmemowner,
			(s32)p->gunctrl.masterloadstate,
			(void *)p->gunctrl.handmodeldef,
			(s32)p->haschrbody);
	}
	forgeApplyDebugRenderEntry();
	/* B-245: capture session stagenum on first activation. */
	if (s_forge.session_stagenum < 0) {
		s_forge.session_stagenum = (s32)g_StageNum;
	}

	/* Issue 8b (2026-04-24): deactivate the Forge IMC when leaving
	 * FREEFLY so X / LB / RB / D-pad revert to their gameplay actions
	 * (USE / WEAPON_PREV / WEAPON_NEXT / FIRE_MODE). Safe to call when
	 * already inactive -- imcDeactivate no-ops in that case. */
	imcDeactivate(&g_ImcForge);

	/* Fix 2+3 (2026-05-01, B-195 follow-up): the Forge editor window
	 * (pdgui_forge_editor.cpp) stops rendering on the next frame because
	 * pdguiForgeEditorRender early-returns on !forgeIsFreefly(). Any ImGui
	 * NavWindow / ActiveID still pointing at editor widgets persists across
	 * the visibility flip and re-asserts WantCaptureKeyboard=true on the
	 * next keypress while the input-context stack reports gameplay-on-top
	 * -- exactly the B-195 leak class. Clear focus + active-id at the
	 * transition seam so the very first NORMAL-mode frame starts clean.
	 * Idempotent: safe to call when ImGui has nothing focused. */
	pdguiClearImGuiFocusAndNav();

	/* Phase 2 fix #9 (2026-05-01, B-195 architectural close): pop the
	 * forge_editor input context. Its on_pop hook also clears ImGui
	 * nav as defense-in-depth; this closes the leak class through the
	 * proper input-stack discipline rather than relying on the seam-
	 * level pdguiClearImGuiFocusAndNav above. The seam-level call stays
	 * for the case where forgeTransitionToNormal fires from a path that
	 * had not previously pushed g_CtxForgeEditor. */
	if (inputCtxIsActive(&g_CtxForgeEditor)) {
		inputCtxPopDeferred(&g_CtxForgeEditor);
	}
}

static void forgeTransitionToFreefly(const char *reason)
{
	struct player *p = forgeCurrentPlayer();
	if (!p) {
		sysLogPrintf(LOG_WARNING, "GRID: cannot enter FREEFLY -- no current player");
		return;
	}
	forgeObserverLayerEnter(reason);
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

	/* B-246 round-6 instrumentation: same purpose as forgeTransitionToNormal
	 * snapshot, captured at FREEFLY entry. */
	if (p == g_Vars.players[0]) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: forgeTransitionToFreefly player=0 reason='%s' wpn=%d switchto=%d gunmemowner=%d masterload=%d handmodeldef=%p haschrbody=%d",
			reason ? reason : "",
			(s32)p->gunctrl.weaponnum,
			(s32)p->gunctrl.switchtoweaponnum,
			(s32)p->gunctrl.gunmemowner,
			(s32)p->gunctrl.masterloadstate,
			(void *)p->gunctrl.handmodeldef,
			(s32)p->haschrbody);
	}
	forgeApplyDebugRenderEntry();
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

	/* Phase 2 fix #9 (input-menu pillar, 2026-05-01, B-195 closure):
	 * push the dedicated g_CtxForgeEditor InputContext. The push
	 * routes the editor through the menu pipeline for cursor + ImGui
	 * keyboard claim while exempting gameplay axes from suppression
	 * (gameplayInputSuppressed checks for this context). The on_pop
	 * hook clears ImGui nav state so the next frame after FREEFLY
	 * exit cannot leak NavWindow / ActiveID into the gameplay
	 * keyboard-capture gate. Idempotent push -- inputCtxPush dedups. */
	if (!inputCtxIsActive(&g_CtxForgeEditor)) {
		inputCtxPush(&g_CtxForgeEditor);
	}
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
	 * Belt-and-braces: unconditionally clear all has_saved_* flags so
	 * the inactive transition is idempotent regardless of currentplayer
	 * state. The integer saved_* fields and saved_chrmodel pointer are
	 * irrelevant once the flags are false (every consumer guards on the
	 * flag); leaving their last-known values keeps the diff minimal.
	 *
	 * Fix 5 (2026-05-01): has_saved_chrmodel joins the clear set.
	 * saved_chrmodel itself is left at its last value but is never read
	 * unless the flag is true. */
	s_forge.fly.has_saved_movemode  = false;
	s_forge.fly.has_saved_body      = false;
	s_forge.fly.has_saved_chrmodel  = false;
	forgeObserverLayerExit(reason);

	/* Phase 2 fix #9 (2026-05-01, B-195 closure): pop the forge_editor
	 * input context if it was pushed (FREEFLY -> Inactive without going
	 * through Normal). Idempotent. */
	if (inputCtxIsActive(&g_CtxForgeEditor)) {
		inputCtxPopDeferred(&g_CtxForgeEditor);
	}

	if (s_forge.state != FORGE_SESSION_INACTIVE) {
		sysLogPrintf(LOG_NOTE, "GRID: -> INACTIVE (%s)", reason ? reason : "");
		forgeRuntimeExitPlay();
	}
	s_forge.state = FORGE_SESSION_INACTIVE;
	forgeApplyDebugRenderExit();
	s_forge.request_enter_session = false;
	s_forge.session_stagenum = -1;  /* B-245: clear so next session captures fresh stagenum */
	s_forge.canvas_mode = false;        /* B-254: clear canvas latch on session end */
	s_forge.request_canvas_mode = false;
	s_forge.observer_layer_active = false;

	/* Issue 8b + AUDIT-24-H2/H3: ensure both Forge IMCs are released
	 * when the session ends through any path (stage-left-gameplay
	 * watchdog, explicit exit, transition to NORMAL is handled
	 * separately). g_ImcForgeSession (FORGE_TOGGLE on Back) is whole-
	 * session scope; g_ImcForge (camera axes / sidebar / tabs) is
	 * FREEFLY scope. Both are idempotent on already-inactive. */
	imcDeactivate(&g_ImcForge);
	imcDeactivate(&g_ImcForgeSession);

	/* Fix 2+3 (2026-05-01, B-195 follow-up): see forgeTransitionToNormal
	 * for the rationale. INACTIVE means the editor will be hidden on every
	 * subsequent frame; clear ImGui focus so any held NavWindow / ActiveID
	 * doesn't outlive the session. */
	pdguiClearImGuiFocusAndNav();

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
	s_forge.canvas_mode = false;          /* B-254: canvas inactive at init */
	s_forge.request_canvas_mode = false;
	s_forge.observer_layer_active = false;
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
	s_forge.request_canvas_mode = false;
	sysLogPrintf(LOG_NOTE, "GRID: enter requested (will activate on gameplay stage load)");
}

void forgeRequestEnterSessionCanvas(void)
{
	s_forge.request_enter_session = true;
	s_forge.request_canvas_mode = true;
	sysLogPrintf(LOG_NOTE, "GRID: enter requested in CANVAS mode (will activate on gameplay stage load)");
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

s32 forgeIsCanvasMode(void)
{
	return (s_forge.state != FORGE_SESSION_INACTIVE && s_forge.canvas_mode) ? 1 : 0;
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

	/* B-244 (2026-04-25): tear down on endscreen. Mission Failed / mainEndStage
	 * sets g_MainIsEndscreen = 1 BEFORE the actual stage transition fires
	 * (mainChangeToStage runs later, after endscreen dismiss). During that
	 * window the stagenum watchdog below cannot fire because g_StageNum is
	 * still the session stage, but the user is at the Mission Failed screen
	 * and trying to use Abort / Restart. If we leave the forge IMCs active,
	 * they intercept gamepad input intended for the endscreen menu and
	 * Abort / Restart go nowhere. Tear down as soon as endscreen flips on. */
	if (s_forge.state != FORGE_SESSION_INACTIVE && g_MainIsEndscreen) {
		forgeTransitionToInactive("endscreen active");
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
			/* B-254 (2026-04-25): latch canvas-mode request now that the
			 * session is going active. Cleared in forgeTransitionToInactive. */
			s_forge.canvas_mode = s_forge.request_canvas_mode;
			s_forge.request_canvas_mode = false;
			if (s_forge.canvas_mode) {
				sysLogPrintf(LOG_NOTE, "GRID: session activating in CANVAS mode (stage=0x%02x)",
						(u32)g_StageNum);
			}
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
