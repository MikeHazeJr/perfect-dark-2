/*
 * inputlayer.c -- Cohort 2 implementation of typed Input Layer Stack.
 * See port/include/inputlayer.h for the contract.
 *
 * Cohort 2 scope began as pure stack mechanics + canonical layer singletons.
 * Later cohorts layered scene dispatch and cutscene entry cleanup onto this
 * stack. The handle-based push/pop discipline remains the load-bearing
 * invariant tested in tests/test_input_layer_stack.cpp.
 */

#include "inputlayer.h"
#include "actionmap.h"
#include "scene.h"
#include <stddef.h>
#include <string.h>

/* ============================================================
 * Storage
 * ============================================================ */

struct LayerHandle {
    s32             slot;        /* index into s_Stack[] */
    s32             generation;  /* bumped each push; zero when empty */
    const LayerDef *def;         /* cached for fast top-type lookup */
    void           *payload;     /* opaque payload threaded through on_push */
};

static struct LayerHandle s_Stack[INPUTLAYER_MAX_DEPTH];
static s32                s_Depth = 0;
static s32                s_NextGeneration = 1;

static const InputAction s_CutsceneActionSet[] = {
    ACTION_SKIP_CUTSCENE,
    ACTION_USE,            /* ACTION_MENU_ACCEPT alias */
    ACTION_CANCEL_USE,     /* ACTION_MENU_CANCEL alias */
    ACTION_FIRE_PRIMARY,
    ACTION_FIRE_SECONDARY,
    ACTION_PAUSE,
    ACTION_FIRE_MODE,
    ACTION_RELOAD,
    ACTION_WEAPON_NEXT,
};

static const InputAction s_VehicleDriverActionSet[] = {
    ACTION_VEHICLE_ACCELERATE,
    ACTION_VEHICLE_BRAKE,
    ACTION_VEHICLE_STEER_LEFT,
    ACTION_VEHICLE_STEER_RIGHT,
    ACTION_VEHICLE_EXIT,
    ACTION_PAUSE,
};

static const InputAction s_ObserverActionSet[] = {
    ACTION_AXIS_MOVE_X,
    ACTION_AXIS_MOVE_Y,
    ACTION_AXIS_AIM_X,
    ACTION_AXIS_AIM_Y,
    ACTION_MOVE_FORWARD,
    ACTION_MOVE_BACKWARD,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_FORGE_TOGGLE,
    ACTION_FORGE_ASCEND,
    ACTION_FORGE_DESCEND,
    ACTION_FORGE_BOOST,
    ACTION_FORGE_PRECISION,
    ACTION_FORGE_SIDEBAR_TOGGLE,
    ACTION_FORGE_SIDEBAR_UP,
    ACTION_FORGE_SIDEBAR_DOWN,
    ACTION_FORGE_SIDEBAR_ACTIVATE,
    ACTION_FORGE_TAB_PREV,
    ACTION_FORGE_TAB_NEXT,
    ACTION_FORGE_PLACE_CANCEL,
    ACTION_FORGE_BOT_ADD,
    ACTION_FORGE_BOT_REMOVE_ALL,
    ACTION_FORGE_BOT_FREEZE_TOGGLE,
    ACTION_FORGE_BOT_SPAWN_CYCLE,
    ACTION_OBSERVER_SUBSET_PREV,
    ACTION_OBSERVER_SUBSET_NEXT,
    ACTION_OBSERVER_MEMBER_PREV,
    ACTION_OBSERVER_MEMBER_NEXT,
    ACTION_OBSERVER_CAMERA_TOGGLE,
    ACTION_OBSERVER_FREEFLY,
    ACTION_OBSERVER_STOP,
    ACTION_OBSERVER_ASCEND,
    ACTION_OBSERVER_DESCEND,
    ACTION_PAUSE,
};

#define INPUTLAYER_ARRAYCOUNT(a) ((s32)(sizeof(a) / sizeof((a)[0])))

/* ============================================================
 * Cohort 4 (2026-04-27, K.2 + K.6): Cutscene layer push/pop hooks.
 *
 * On push (cutscene start):
 *   1. Activate g_ImcCutscene so ACTION_SKIP_CUTSCENE binds win.
 *   2. Flush gameplay-only action state for ALL players.
 *   3. Flush the cutscene action set, including shared accept/cancel state.
 *
 * The flush is the belt of the flash fix; the actionPressed
 * (edge) skip detection at player.c (K.6) is the braces.
 * Together they make the Mission 1 obj 2 cutscene flash
 * structurally impossible: any held gameplay-only action clears at
 * push, and any held shared skip action (ACTION_USE / menu accept,
 * ACTION_PAUSE, etc.) is explicitly cleared by the layer action-set
 * flush before the cutscene tick can observe it.
 *
 * On pop (cutscene end): deactivate g_ImcCutscene only. Do NOT
 * flush again on exit -- gameplay state should resume cleanly
 * with whatever the player happens to be pressing AT exit.
 * ============================================================ */

static int onCutscenePush(void *payload)
{
    (void)payload;
    imcCutsceneEnter();
    actionmapFlushGameplayState();
    actionmapFlushActionSet(s_CutsceneActionSet, INPUTLAYER_ARRAYCOUNT(s_CutsceneActionSet));
    return 0;
}

static void onCutscenePop(void *result_out)
{
    (void)result_out;
    imcCutsceneExit();
}

static void onCutsceneAbort(int reason_code)
{
    (void)reason_code;
    imcCutsceneExit();
}

static int onVehicleDriverPush(void *payload)
{
    (void)payload;
    imcVehicleMount();
    actionmapFlushGameplayState();
    actionmapFlushActionSet(s_VehicleDriverActionSet, INPUTLAYER_ARRAYCOUNT(s_VehicleDriverActionSet));
    return 0;
}

static void onVehicleDriverPop(void *result_out)
{
    (void)result_out;
    imcVehicleDismount();
    actionmapFlushActionSet(s_VehicleDriverActionSet, INPUTLAYER_ARRAYCOUNT(s_VehicleDriverActionSet));
}

static void onVehicleDriverAbort(int reason_code)
{
    (void)reason_code;
    imcVehicleDismount();
    actionmapFlushActionSet(s_VehicleDriverActionSet, INPUTLAYER_ARRAYCOUNT(s_VehicleDriverActionSet));
}

/* s036-06 (c036): track which source pushed the observer layer so pop /
 * abort release exactly what push acquired. -1 means inactive. */
static int s_ObserverActiveSource = -1;

static int onObserverPush(void *payload)
{
    const SceneObserverPayload *observer_payload = (const SceneObserverPayload *)payload;
    s_ObserverActiveSource = (observer_payload ? observer_payload->source : -1);
    if (observer_payload && observer_payload->source == SCENE_OBSERVER_SOURCE_SPECTATOR) {
        imcActivate(&g_ImcObserver);
    }
    actionmapFlushActionSet(s_ObserverActionSet, INPUTLAYER_ARRAYCOUNT(s_ObserverActionSet));
    return 0;
}

static void onObserverPop(void *result_out)
{
    (void)result_out;
    actionmapFlushActionSet(s_ObserverActionSet, INPUTLAYER_ARRAYCOUNT(s_ObserverActionSet));
    /* s036-06: symmetric deactivate. Only release what THIS push acquired.
     * Forge IMCs (g_ImcForge / g_ImcForgeSession) are owned by forge transition
     * code in src/game/forgemode.c -- LAYER_OBSERVER is a wrapper around forge
     * freefly, not its owner. */
    if (s_ObserverActiveSource == SCENE_OBSERVER_SOURCE_SPECTATOR) {
        imcDeactivate(&g_ImcObserver);
    }
    s_ObserverActiveSource = -1;
}

static void onObserverAbort(int reason_code)
{
    (void)reason_code;
    actionmapFlushActionSet(s_ObserverActionSet, INPUTLAYER_ARRAYCOUNT(s_ObserverActionSet));
    /* s036-06: symmetric deactivate (see onObserverPop). */
    if (s_ObserverActiveSource == SCENE_OBSERVER_SOURCE_SPECTATOR) {
        imcDeactivate(&g_ImcObserver);
    }
    s_ObserverActiveSource = -1;
}

/* ============================================================
 * Canonical layer singletons
 * ============================================================ */

const LayerDef g_LayerBoot = {
    .type                  = LAYER_BOOT,
    .name                  = "Boot",
    .action_set            = NULL,
    .action_set_count      = 0,
    .on_push               = NULL,
    .on_pop                = NULL,
    .on_abort              = NULL,
    .imc                   = NULL,
    .wants_relative_mouse  = 0,
    .wants_visible_cursor  = 0,
};

const LayerDef g_LayerGameplay = {
    .type                  = LAYER_GAMEPLAY,
    .name                  = "Gameplay",
    .action_set            = NULL,
    .action_set_count      = 0,
    .on_push               = NULL,
    .on_pop                = NULL,
    .on_abort              = NULL,
    /* s036-01 (c036): wire the binding declaratively. g_ImcGameplay is the
     * priority-0 baseline activated once at startup by actionmap init
     * (port/src/actionmap.cpp imcActivate at end of actionmapInit) and stays
     * on for the life of the process; LAYER_GAMEPLAY push/pop intentionally
     * does NOT toggle it via on_push/on_pop. The .imc field documents the
     * binding for introspection and future migration to a layer-driven
     * activation model. */
    .imc                   = &g_ImcGameplay,
    .wants_relative_mouse  = 1,
    .wants_visible_cursor  = 0,
};

const LayerDef g_LayerCutscene = {
    .type                  = LAYER_CUTSCENE,
    .name                  = "Cutscene",
    .action_set            = s_CutsceneActionSet,
    .action_set_count      = INPUTLAYER_ARRAYCOUNT(s_CutsceneActionSet),
    .on_push               = onCutscenePush,    /* Cohort 4: flash fix belt */
    .on_pop                = onCutscenePop,     /* Cohort 4: IMC deactivate on exit */
    .on_abort              = onCutsceneAbort,   /* Cohort 4: same cleanup on abort */
    .imc                   = &g_ImcCutscene,    /* Cohort 4 wired (K.2) */
    .wants_relative_mouse  = 0,
    .wants_visible_cursor  = 0,
};

const LayerDef g_LayerMenu = {
    .type                  = LAYER_MENU,
    .name                  = "Menu",
    .action_set            = NULL,
    .action_set_count      = 0,
    .on_push               = NULL,
    .on_pop                = NULL,
    .on_abort              = NULL,
    /* s036-01 (c036): wire the binding declaratively. g_ImcMenu lifecycle
     * is owned by the input CONTEXT stack (port/src/inputctx.c imcActivate /
     * imcDeactivate inside g_CtxImGuiMenu / g_CtxPauseMenu / g_CtxDebugOverlay
     * push and pop), NOT by LAYER_MENU push/pop. Wiring it here at the layer
     * level would double-fire and cause shadowing bugs (see commentary at
     * port/src/actionmap.cpp:2947 - activating ImcMenu unconditionally at
     * startup shadowed gameplay gamepad bindings). The .imc field documents
     * the binding for introspection. */
    .imc                   = &g_ImcMenu,
    .wants_relative_mouse  = 0,
    .wants_visible_cursor  = 1,
};

const LayerDef g_LayerVehicleDriver = {
    .type                  = LAYER_VEHICLE_DRIVER,
    .name                  = "VehicleDriver",
    .action_set            = s_VehicleDriverActionSet,
    .action_set_count      = INPUTLAYER_ARRAYCOUNT(s_VehicleDriverActionSet),
    .on_push               = onVehicleDriverPush,
    .on_pop                = onVehicleDriverPop,
    .on_abort              = onVehicleDriverAbort,
    .imc                   = &g_ImcVehicle,
    .wants_relative_mouse  = 1,
    .wants_visible_cursor  = 0,
};

const LayerDef g_LayerVehicleTurret = {
    .type                  = LAYER_VEHICLE_TURRET,
    .name                  = "VehicleTurret",
    .action_set            = NULL,
    .action_set_count      = 0,
    .on_push               = NULL,
    .on_pop                = NULL,
    .on_abort              = NULL,
    .imc                   = NULL, /* greenfield */
    .wants_relative_mouse  = 1,
    .wants_visible_cursor  = 0,
};

const LayerDef g_LayerObserver = {
    .type                  = LAYER_OBSERVER,
    .name                  = "Observer",
    .action_set            = s_ObserverActionSet,
    .action_set_count      = INPUTLAYER_ARRAYCOUNT(s_ObserverActionSet),
    .on_push               = onObserverPush,
    .on_pop                = onObserverPop,
    .on_abort              = onObserverAbort,
    .imc                   = &g_ImcObserver,
    .wants_relative_mouse  = 1,
    .wants_visible_cursor  = 0,
};

/* ============================================================
 * Internal helpers
 * ============================================================ */

static int handleIsValid(const LayerHandle *h)
{
    if (!h) return 0;
    if (h->slot < 0 || h->slot >= s_Depth) return 0;
    if (h->generation == 0) return 0;
    if (s_Stack[h->slot].generation != h->generation) return 0;
    return 1;
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void inputLayerInit(void)
{
    /* Cascade-abort anything that's still on the stack from prior boot. */
    if (s_Depth > 0) {
        inputLayerAbort(s_Depth, /* INPUTLAYER_ABORT_REINIT */ -1);
    }
    memset(s_Stack, 0, sizeof(s_Stack));
    s_Depth = 0;
    s_NextGeneration = 1;
    /* Push BOOT as the always-on bottom of the stack. */
    inputLayerPush(&g_LayerBoot, NULL);
}

void inputLayerShutdown(void)
{
    if (s_Depth > 0) {
        inputLayerAbort(s_Depth, /* INPUTLAYER_ABORT_SHUTDOWN */ -2);
    }
    s_Depth = 0;
    s_NextGeneration = 1;
}

/* ============================================================
 * Stack API
 * ============================================================ */

LayerHandle *inputLayerPush(const LayerDef *def, void *payload)
{
    if (!def) return NULL;
    if (s_Depth >= INPUTLAYER_MAX_DEPTH) return NULL;

    s32 slot = s_Depth;
    struct LayerHandle *h = &s_Stack[slot];
    h->slot       = slot;
    h->generation = s_NextGeneration++;
    if (s_NextGeneration <= 0) s_NextGeneration = 1; /* wrap, skip 0 */
    h->def        = def;
    h->payload    = payload;
    s_Depth++;

    if (def->on_push) {
        (void)def->on_push(payload);
    }
    return (LayerHandle *)h;
}

s32 inputLayerPop(LayerHandle *h, void *result_out)
{
    if (!handleIsValid(h)) return -1;
    if (h->slot != s_Depth - 1) return -2; /* not on top */

    const LayerDef *def = h->def;
    if (def && def->on_pop) {
        def->on_pop(result_out);
    }
    /* Invalidate this handle (and any stale handle pointing at this slot). */
    s_Stack[h->slot].generation = 0;
    s_Stack[h->slot].def        = NULL;
    s_Stack[h->slot].payload    = NULL;
    s_Depth--;
    return 0;
}

void inputLayerAbort(s32 from_top, s32 reason_code)
{
    if (from_top <= 0) return;
    if (from_top > s_Depth) from_top = s_Depth;

    for (s32 i = 0; i < from_top; i++) {
        s32 slot = s_Depth - 1;
        if (slot < 0) break;
        const LayerDef *def = s_Stack[slot].def;
        if (def && def->on_abort) {
            def->on_abort(reason_code);
        }
        s_Stack[slot].generation = 0;
        s_Stack[slot].def        = NULL;
        s_Stack[slot].payload    = NULL;
        s_Depth--;
    }
}

/* ============================================================
 * Inspection
 * ============================================================ */

LayerHandle *inputLayerTop(void)
{
    if (s_Depth <= 0) return NULL;
    return (LayerHandle *)&s_Stack[s_Depth - 1];
}

LayerType inputLayerTopType(void)
{
    if (s_Depth <= 0) return LAYER_TYPE_COUNT;
    const LayerDef *def = s_Stack[s_Depth - 1].def;
    if (!def) return LAYER_TYPE_COUNT;
    return def->type;
}

s32 inputLayerDepth(void)
{
    return s_Depth;
}

s32 inputLayerHas(LayerType t)
{
    for (s32 i = 0; i < s_Depth; i++) {
        const LayerDef *def = s_Stack[i].def;
        if (def && def->type == t) return 1;
    }
    return 0;
}

const LayerDef *inputLayerHandleDef(const LayerHandle *h)
{
    if (!handleIsValid(h)) return NULL;
    return h->def;
}

s32 inputLayerHandleDistanceFromTop(const LayerHandle *h)
{
    if (!handleIsValid(h)) return -1;
    return s_Depth - h->slot;
}
