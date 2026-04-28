/**
 * inputlayer.h -- Cohort 2 of input-universality-and-transitions.
 *
 * Typed Input Layer Stack. Each layer declares its LayerType, the actions
 * it owns, an optional IMC, push/pop/abort callbacks, and a cursor-mode
 * preference. Push and pop are explicit. Only the topmost layer fires
 * actions (Cohort 7 wires the dispatch-side gate; this cohort just
 * provides the data structure + invariants).
 *
 * The existing IMC stack (port/src/inputctx.c) stays as the binding
 * resolution layer; the Layer Stack sits above it as the activation +
 * action-set layer. Cohort 3 introduces the Scene Manager that drives
 * push/pop in response to scene events. Cohort 4 wires the cutscene
 * flash fix on top of this scaffolding.
 *
 * Cohort 2 deliberately does NOT change `gameplayInputSuppressed()`,
 * does NOT hook actionmap dispatch, and does NOT push from any real
 * site. It only adds the stack module + tests so later cohorts can
 * land their migrations in bisectable steps.
 *
 * @design context/designs/input-universality-and-transitions-2026-04-27.md SC
 *
 * Logging channels reserved for future runtime diagnostics:
 *   INPUT.LAYER.PUSH name=<layer> depth=<n>
 *   INPUT.LAYER.POP  name=<layer> result=<reason>
 *   INPUT.LAYER.ABORT from_top=<n> reason=<code>
 */

#ifndef _IN_INPUTLAYER_H
#define _IN_INPUTLAYER_H

#include <PR/ultratypes.h>
#include "actionmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * LayerType -- the seven first-class scenes
 * ============================================================ */

typedef enum LayerType {
    LAYER_BOOT = 0,           /* pre-stage-load, splash, copyright */
    LAYER_GAMEPLAY,           /* normal play (bondmove + bondgun) */
    LAYER_CUTSCENE,           /* cutscene tick (per-player in Cohort 4) */
    LAYER_MENU,               /* any menu / overlay / dialog */
    LAYER_VEHICLE_DRIVER,     /* mounted vehicle, driver seat */
    LAYER_VEHICLE_TURRET,     /* future, gunner seat */
    LAYER_OBSERVER,           /* Dr Carroll free-fly + Forge freefly */
    LAYER_TYPE_COUNT,         /* sentinel -- keep last */
} LayerType;

/* ============================================================
 * Capacity
 * ============================================================ */

#define INPUTLAYER_MAX_DEPTH 16

/* ============================================================
 * Callbacks
 * ============================================================ */

typedef int  (*LayerOnPushFn) (void *payload);
typedef void (*LayerOnPopFn)  (void *result_out);
typedef void (*LayerOnAbortFn)(int reason_code);

/* ============================================================
 * LayerDef -- declarative layer specification
 * ============================================================ */

typedef struct LayerDef {
    LayerType            type;
    const char          *name;             /* "Boot", "Gameplay", "Cutscene", ... */
    const InputAction   *action_set;       /* owned action ids; NULL = empty */
    s32                  action_set_count; /* length of action_set */
    LayerOnPushFn        on_push;          /* may be NULL */
    LayerOnPopFn         on_pop;           /* may be NULL */
    LayerOnAbortFn       on_abort;         /* may be NULL */
    InputMappingContext *imc;              /* may be NULL (e.g., LAYER_BOOT) */
    s32                  wants_relative_mouse; /* SDL_bool surrogate (0/1) */
    s32                  wants_visible_cursor; /* SDL_bool surrogate (0/1) */
} LayerDef;

/* Opaque handle returned by push, consumed by pop. */
typedef struct LayerHandle LayerHandle;

/* ============================================================
 * Lifecycle
 * ============================================================ */

/** Reset stack to empty + push LAYER_BOOT. Idempotent. */
void inputLayerInit(void);

/** Tear down: aborts every layer via abort cascade and clears state. */
void inputLayerShutdown(void);

/* ============================================================
 * Stack API
 * ============================================================ */

/** Push a layer. `def` must be non-NULL. `payload` is forwarded to
 *  on_push (may be NULL). Returns a handle to be passed to pop on
 *  unwind. Returns NULL on capacity overflow (logged at runtime).
 *  Cohort 2 does NOT yet activate the IMC; that wires in Cohort 3+. */
LayerHandle *inputLayerPush(const LayerDef *def, void *payload);

/** Pop the top layer. Caller passes the handle returned by push to
 *  guard against out-of-order pops; mismatched handles cause a no-op
 *  and a non-zero return. `result_out` is forwarded to on_pop and may
 *  be NULL. Returns 0 on success, non-zero on error. */
s32 inputLayerPop(LayerHandle *h, void *result_out);

/** Pop the top `from_top` layers, calling each layer's on_abort with
 *  `reason_code` on the way down. Used by stage teardown / disconnect
 *  / fatal error paths to unwind without per-call book-keeping. */
void inputLayerAbort(s32 from_top, s32 reason_code);

/* ============================================================
 * Inspection
 * ============================================================ */

/** The topmost layer's handle (NULL when empty). */
LayerHandle *inputLayerTop(void);

/** The topmost layer's type (LAYER_TYPE_COUNT when empty). */
LayerType inputLayerTopType(void);

/** Depth of the stack (0 = empty, including freshly-init state where
 *  nothing has been pushed). After inputLayerInit(), depth is 1. */
s32 inputLayerDepth(void);

/** True iff any layer of the given type is currently in the stack. */
s32 inputLayerHas(LayerType t);

/** The LayerDef for a given handle (NULL when handle is invalid). */
const LayerDef *inputLayerHandleDef(const LayerHandle *h);

/* ============================================================
 * Canonical layer singletons (declared in inputlayer.c)
 *
 * Cohort 2 declares one LayerDef per LayerType with sensible defaults.
 * Cohort 3 will populate `imc` and `action_set` to match the design
 * catalog (Section E). Callers must pass these to inputLayerPush;
 * future code will get a `inputLayerPushType(LayerType)` shorthand.
 * ============================================================ */

extern const LayerDef g_LayerBoot;
extern const LayerDef g_LayerGameplay;
extern const LayerDef g_LayerCutscene;
extern const LayerDef g_LayerMenu;
extern const LayerDef g_LayerVehicleDriver;
extern const LayerDef g_LayerVehicleTurret;
extern const LayerDef g_LayerObserver;

#ifdef __cplusplus
}
#endif

#endif /* _IN_INPUTLAYER_H */
