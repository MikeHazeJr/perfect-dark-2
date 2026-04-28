/**
 * scene.h -- Cohort 3 of input-universality-and-transitions.
 *
 * Scene / State Manager. Coordinates layer transitions with scene
 * events. Game code emits events via sceneFire(); this module
 * translates them into inputLayer push/pop calls.
 *
 * Cohort 3 deliberately ships the dispatcher only. Real callsite
 * wiring (mainTickStage / playerStartCutscene / playerEndCutscene /
 * pause / vehicle board / observer enter) lands in Cohort 4 onward
 * so each migration can be bisected independently. Cohort 3 also
 * does NOT yet activate IMCs on layer push -- that is part of
 * Cohort 7's raw-input migration sweep.
 *
 * @design context/designs/input-universality-and-transitions-2026-04-27.md SD
 *
 * Logging channels reserved for runtime diagnostics:
 *   TRANSITION.SCENE event=<name> from=<layer> to=<layer> reason=<...>
 */

#ifndef _IN_SCENE_H
#define _IN_SCENE_H

#include <PR/ultratypes.h>
#include "inputlayer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SceneEvent -- the canonical scene transition messages
 * ============================================================ */

typedef enum SceneEvent {
    SCENE_EVENT_BOOT_COMPLETE = 0, /* pops LAYER_BOOT, pushes GAMEPLAY */
    SCENE_EVENT_STAGE_LOADING,     /* stage swap in flight (advisory) */
    SCENE_EVENT_STAGE_READY,       /* new stage loaded, gameplay can begin */
    SCENE_EVENT_GAMEPLAY_START,    /* synonym of STAGE_READY for clarity */
    SCENE_EVENT_CUTSCENE_START,    /* payload: SceneCutscenePayload */
    SCENE_EVENT_CUTSCENE_END,      /* payload: SceneCutscenePayload (reason) */
    SCENE_EVENT_PAUSE_OPEN,        /* push LAYER_MENU (pause variant) */
    SCENE_EVENT_PAUSE_CLOSE,       /* pop LAYER_MENU */
    SCENE_EVENT_VEHICLE_BOARD,     /* payload: SceneVehiclePayload */
    SCENE_EVENT_VEHICLE_DISMOUNT,  /* payload: SceneVehiclePayload */
    SCENE_EVENT_OBSERVER_ENTER,    /* payload: SceneObserverPayload */
    SCENE_EVENT_OBSERVER_EXIT,     /* payload: SceneObserverPayload */
    SCENE_EVENT_STAGE_TEARDOWN,    /* abort cascade to BOOT */
    SCENE_EVENT_DISCONNECT,        /* abort cascade to MENU */
    SCENE_EVENT_COUNT              /* sentinel -- keep last */
} SceneEvent;

/* ============================================================
 * Payload structs (passed via the void *payload argument of sceneFire)
 * ============================================================ */

/* Cutscene start / end payload. Cohort 4 will populate player_mask;
 * Cohort 3 accepts mask=0 as "all players" for back-compat. */
typedef struct SceneCutscenePayload {
    s16 anim_num;
    u8  player_mask;     /* 0 = all players (Cohort 3 default) */
    u8  trigger_source;  /* SCENE_CUTSCENE_TRIGGER_* (see below) */
    s32 reason;          /* end-only: SCENE_CUTSCENE_END_REASON_* */
} SceneCutscenePayload;

#define SCENE_CUTSCENE_TRIGGER_AI_SCRIPT  0
#define SCENE_CUTSCENE_TRIGGER_DEBUG      1
#define SCENE_CUTSCENE_TRIGGER_AUTOCUT    2

#define SCENE_CUTSCENE_END_REASON_NATURAL 0
#define SCENE_CUTSCENE_END_REASON_SKIP    1
#define SCENE_CUTSCENE_END_REASON_ABORT   2

/* Vehicle board / dismount payload. Cohort 5 populates these. */
typedef struct SceneVehiclePayload {
    s32   vehicle_kind;  /* SCENE_VEHICLE_KIND_* */
    void *prop;          /* opaque struct prop *, may be NULL in tests */
} SceneVehiclePayload;

#define SCENE_VEHICLE_KIND_DRIVER 0
#define SCENE_VEHICLE_KIND_TURRET 1

/* Observer enter / exit payload. */
typedef struct SceneObserverPayload {
    s32 source;          /* SCENE_OBSERVER_SOURCE_* */
} SceneObserverPayload;

#define SCENE_OBSERVER_SOURCE_DR_CARROLL 0
#define SCENE_OBSERVER_SOURCE_FORGE      1
#define SCENE_OBSERVER_SOURCE_DEBUG_FLY  2
#define SCENE_OBSERVER_SOURCE_SPECTATOR  3

/* ============================================================
 * Lifecycle
 * ============================================================ */

/** Initialize the scene manager. Idempotent. Must be called after
 *  inputLayerInit() so SCENE_EVENT_BOOT_COMPLETE has a layer stack
 *  to operate on. */
void sceneInit(void);

/** Tear down the scene manager (no layer ops; inputLayerShutdown is
 *  the layer-side authority). */
void sceneShutdown(void);

/* ============================================================
 * Dispatch
 * ============================================================ */

/** Fire a scene event. Translates into the appropriate inputLayer
 *  push/pop/abort call. `payload` is event-specific (see structs
 *  above) and may be NULL when the event has no required payload.
 *  Returns 0 on success, non-zero on rejection (e.g., illegal
 *  current-top combination, missing payload).
 *
 *  This function is idempotent for already-applied events (e.g.,
 *  pushing CUTSCENE when already on CUTSCENE returns 0 without
 *  pushing a duplicate). The dispatcher logs every transition
 *  via the TRANSITION.SCENE channel. */
s32 sceneFire(SceneEvent ev, const void *payload);

/** The current top-level scene layer type. Wrapper around
 *  inputLayerTopType() so callers do not need to include
 *  inputlayer.h directly. */
LayerType sceneCurrentLayer(void);

/* ============================================================
 * Test instrumentation (counted dispatch events, optional)
 * ============================================================ */

typedef struct SceneFireCounts {
    s32 fire_count[SCENE_EVENT_COUNT];
    s32 reject_count[SCENE_EVENT_COUNT];
} SceneFireCounts;

/** Reset counters to zero. Returns the prior snapshot via `out` if
 *  non-NULL. */
void sceneInstrumentReset(SceneFireCounts *out);

/** Read-only access to the live counter. */
const SceneFireCounts *sceneInstrumentGet(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SCENE_H */
