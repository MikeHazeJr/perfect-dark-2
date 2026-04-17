/**
 * forgemode.h -- Phase F0 Foundation: in-game level editor mode toggle.
 *
 * Forge sessions live inside an existing gameplay stage (CI Training in F0,
 * any base stage in F3+).  Within a session the player toggles between two
 * sub-modes:
 *
 *   FORGE_SESSION_NORMAL   first-person play (standard gameplay, untouched)
 *   FORGE_SESSION_FREEFLY  free-fly Dr. Carroll editor camera (chr frozen
 *                          via MOVEMODE_CUTSCENE; pos/look overridden each tick)
 *
 * F0 scope: state machine, mode toggle (ACTION_FORGE_TOGGLE), 6DOF camera,
 * minimal HUD shell, no-collision ghost camera, single base stage entry.
 * Catalog/placement/gizmo/properties/serialization land in F1-F8.
 *
 * Design ref: context/designs/forge-level-editor-2026-04-16.md
 *
 * C-callable; included from C++ via the extern "C" block.
 */

#ifndef _IN_GAME_FORGEMODE_H
#define _IN_GAME_FORGEMODE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: types.h defines `struct coord` but also `#define bool s32`
 * which breaks C++ translation units that include this header. */
struct coord;

typedef enum forge_session_state {
	FORGE_SESSION_INACTIVE = 0, /* no forge session — normal gameplay or menu */
	FORGE_SESSION_NORMAL   = 1, /* in session, first-person play */
	FORGE_SESSION_FREEFLY  = 2, /* in session, free-fly editor camera */
} forge_session_state_t;

/* ============================================================
 * Lifecycle
 * ============================================================ */

/** One-shot init.  Idempotent.  Called from main init once at startup. */
void forgeInit(void);

/** Per-frame tick.  Drives mode toggle, freefly camera math, and the
 *  session-exit watchdog (resets to INACTIVE on stage transition out of
 *  gameplay).  Called once per frame from mainTick after lvTick(), before
 *  the per-player gameplay loop runs. */
void forgeTick(void);

/* ============================================================
 * Session control (called from menu code)
 * ============================================================ */

/** Mark "enter forge session as soon as a gameplay stage is loaded".
 *  Call from the main menu Forge button right before mainChangeToStage().
 *  forgeTick consumes the request once g_StageNum becomes a gameplay stage. */
void forgeRequestEnterSession(void);

/** Force-exit the session immediately.  Restores the player's prior
 *  bondmovemode if FREEFLY was active.  Safe to call from any context.
 *  Called automatically from forgeTick when leaving gameplay. */
void forgeExitSession(void);

/* ============================================================
 * State queries
 * ============================================================ */

forge_session_state_t forgeGetSessionState(void);

/** Convenience wrappers.  Returned as s32 (1/0) for C++ ABI compat
 *  (project's `bool` is typedef-aliased to s32 in C, but C++ has a
 *  native bool with a different size). */
s32 forgeSessionIsActive(void);  /* state != INACTIVE */
s32 forgeIsFreefly(void);        /* state == FREEFLY  */

/* ============================================================
 * Free-fly camera state (read-only access for HUD / debug)
 * ============================================================ */

void forgeGetCameraPos(struct coord *out_pos);
f32  forgeGetCameraYawDeg(void);
f32  forgeGetCameraPitchDeg(void);
f32  forgeGetCurrentSpeedScale(void); /* 1.0 default, 3.0 boost, 0.25 precision */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_GAME_FORGEMODE_H */
