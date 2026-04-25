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

/** B-254 (2026-04-25): mark "enter forge session in CANVAS mode".  Same as
 *  forgeRequestEnterSession but additionally sets the canvas-mode flag so
 *  setup-time chr/AI/script creation paths suppress NPC spawns, mission
 *  triggers, scripted door behaviors, cutscene intros, and objective
 *  markers.  Geometry, lighting, doors-as-physical-objects, and vehicles-
 *  as-static-props still load.  Used when a SP campaign mission is picked
 *  as a Grid build canvas (catalog entry has
 *  ext.arena.load_mode == ARENA_LOADMODE_CANVAS). */
void forgeRequestEnterSessionCanvas(void);

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

/** B-254 (2026-04-25): true when the active session is in CANVAS mode.
 *  Setup-time and per-tick callers query this to suppress mission-side
 *  effects (chr spawns, scripts, cutscenes, objective markers) while
 *  preserving geometry / lighting / doors / static vehicles.  Returns
 *  0 outside a canvas session. */
s32 forgeIsCanvasMode(void);

/* ============================================================
 * Free-fly camera state (read-only access for HUD / debug)
 * ============================================================ */

void forgeGetCameraPos(struct coord *out_pos);
f32  forgeGetCameraYawDeg(void);
f32  forgeGetCameraPitchDeg(void);
f32  forgeGetCurrentSpeedScale(void); /* 1.0 default, 3.0 boost, 0.25 precision */

/* ============================================================
 * Per-player session tracking (foundation for MP co-op forge)
 *
 * In a MP co-op forge session each human player can independently be in
 * FREEFLY (editor) or NORMAL (playtest) mode.  Single-player sessions
 * currently use playerNum=0 only; wire carries per-player state for the
 * future.  Stage reload is NEVER triggered by a sub-mode toggle -- the
 * transition is purely a bondmovemode swap + pos hijack.
 * ============================================================ */

/** Returns the per-player editor sub-mode (NORMAL or FREEFLY) if that
 *  player is currently in-session.  Returns FORGE_SESSION_INACTIVE
 *  for players outside the session. F0 impl: all queries proxy to the
 *  global state (only playerNum=0 is meaningful on PC). */
forge_session_state_t forgeGetPlayerSessionState(s32 playerNum);

/** Request a seamless sub-mode toggle for a specific player.  Does NOT
 *  reload the stage.  Entering FREEFLY snaps Dr. Carroll's camera to
 *  the player's current pos; exiting FREEFLY puts the player back into
 *  NORMAL at the freefly camera's final position.  The player chr's
 *  bodynum/headnum are saved on entry and restored on exit so a future
 *  engine-level hot-swap can put the Dr. Carroll model visible to other
 *  players during FREEFLY. */
void forgeTogglePlayerMode(s32 playerNum);

/* ============================================================
 * Level-tab accessors (Issue 9 / Priority C, 2026-04-24)
 *
 * C-opaque read / write for the forge editor's Level tab ambience
 * controls.  The editor is in a C++ TU that cannot include types.h
 * (bool/s32 conflict), so all reads and writes to struct environment
 * go through these wrappers.  Changes take effect immediately (sky
 * color / fog / clouds / skybox repaint on next tick; music starts
 * on the next audio tick).
 * ============================================================ */

void forgeLevelGetSkyColor(u8 *out_r, u8 *out_g, u8 *out_b);
void forgeLevelSetSkyColor(u8 r, u8 g, u8 b);

void forgeLevelGetFog(s32 *out_fogmin, s32 *out_fogmax);
void forgeLevelSetFog(s32 fogmin, s32 fogmax);

s32  forgeLevelGetCloudsEnabled(void);
void forgeLevelSetCloudsEnabled(s32 enabled);

void forgeLevelGetCloudColor(f32 *out_r, f32 *out_g, f32 *out_b);
void forgeLevelSetCloudColor(f32 r, f32 g, f32 b);

/** Swap the live environment + skybox to another stage's sky.  No-op for
 *  non-positive stagenums.  Use to preview how a forge map looks under
 *  Villa's sky, Skedar Ruins' alien sun, etc. */
void forgeLevelSetSkyStage(s32 stagenum);

/** Start a different primary music track immediately.  Stops on
 *  forgeLevelStopMusic or on next musicReset (scene transition). */
void forgeLevelPlayMusic(s32 tracknum);
void forgeLevelStopMusic(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_GAME_FORGEMODE_H */
