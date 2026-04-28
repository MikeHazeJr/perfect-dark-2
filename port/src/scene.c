/*
 * scene.c -- Cohort 3 implementation of the Scene / State Manager.
 * See port/include/scene.h for the contract.
 *
 * Cohort 3 scope: dispatcher + push/pop/abort translation. No real
 * callsite is wired yet; production code paths still use the
 * pre-existing input/menu/cutscene mechanisms. Cohort 4 wires
 * playerStartCutscene + playerEndCutscene to fire CUTSCENE_START /
 * CUTSCENE_END as part of the flash fix.
 */

#include "scene.h"
#include "inputlayer.h"
#include <stddef.h>
#include <string.h>

/* ============================================================
 * Per-event handle tracking
 *
 * For events that push a layer (CUTSCENE_START / PAUSE_OPEN /
 * VEHICLE_BOARD / OBSERVER_ENTER), we cache the LayerHandle so the
 * matching close event (_END / _CLOSE / _DISMOUNT / _EXIT) can pop
 * the exact instance even if other layers stacked on top in between.
 *
 * This is single-instance per event family. Cohort 4 introduces
 * per-player cutscene state which moves CUTSCENE handle storage
 * into struct player; for Cohort 3's dispatcher tests it is enough
 * to track one global slot per event.
 * ============================================================ */

static LayerHandle *s_CutsceneHandle    = NULL;
static LayerHandle *s_PauseHandle       = NULL;
static LayerHandle *s_VehicleHandle     = NULL;
static LayerHandle *s_ObserverHandle    = NULL;
static s32          s_ObserverSource    = -1;
static SceneObserverPayload s_ObserverPayload;

static SceneFireCounts s_Counts;

static void sceneClearTrackedHandlesInRange(s32 from_top)
{
    s32 d;

    if (from_top <= 0) {
        return;
    }

    d = s_CutsceneHandle ? inputLayerHandleDistanceFromTop(s_CutsceneHandle) : 0;
    if (d < 0 || (d > 0 && d <= from_top)) {
        s_CutsceneHandle = NULL;
    }

    d = s_PauseHandle ? inputLayerHandleDistanceFromTop(s_PauseHandle) : 0;
    if (d < 0 || (d > 0 && d <= from_top)) {
        s_PauseHandle = NULL;
    }

    d = s_VehicleHandle ? inputLayerHandleDistanceFromTop(s_VehicleHandle) : 0;
    if (d < 0 || (d > 0 && d <= from_top)) {
        s_VehicleHandle = NULL;
    }

    d = s_ObserverHandle ? inputLayerHandleDistanceFromTop(s_ObserverHandle) : 0;
    if (d < 0 || (d > 0 && d <= from_top)) {
        s_ObserverHandle = NULL;
        s_ObserverSource = -1;
        s_ObserverPayload.source = -1;
    }
}

static void scenePopTrackedLayer(LayerHandle **handle_slot, SceneEvent ev)
{
    if (!handle_slot || !*handle_slot) {
        return;
    }

    LayerHandle *h = *handle_slot;
    s32 rc = inputLayerPop(h, NULL);
    if (rc != 0) {
        s_Counts.reject_count[ev]++;

        if (rc == -2) {
            s32 from_top = inputLayerHandleDistanceFromTop(h);
            if (from_top > 0) {
                sceneClearTrackedHandlesInRange(from_top);
                inputLayerAbort(from_top, /* nested close */ 0);
            }
        }
    }

    *handle_slot = NULL;
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void sceneInit(void)
{
    s_CutsceneHandle = NULL;
    s_PauseHandle    = NULL;
    s_VehicleHandle  = NULL;
    s_ObserverHandle = NULL;
    s_ObserverSource = -1;
    s_ObserverPayload.source = -1;
    memset(&s_Counts, 0, sizeof(s_Counts));
}

void sceneShutdown(void)
{
    s_CutsceneHandle = NULL;
    s_PauseHandle    = NULL;
    s_VehicleHandle  = NULL;
    s_ObserverHandle = NULL;
    s_ObserverSource = -1;
    s_ObserverPayload.source = -1;
}

/* ============================================================
 * Dispatch
 * ============================================================ */

LayerType sceneCurrentLayer(void)
{
    return inputLayerTopType();
}

s32 sceneFire(SceneEvent ev, const void *payload)
{
    if (ev < 0 || ev >= SCENE_EVENT_COUNT) return -1;

    s_Counts.fire_count[ev]++;

    switch (ev) {
    case SCENE_EVENT_BOOT_COMPLETE:
        /* Replace BOOT with GAMEPLAY at the bottom of the stack.
         * Pop everything down to BOOT, then push GAMEPLAY on top
         * (BOOT remains as the always-on root). */
        inputLayerAbort(inputLayerDepth() - 1, /* boot reason */ 0);
        if (inputLayerTopType() == LAYER_BOOT) {
            inputLayerPush(&g_LayerGameplay, NULL);
            return 0;
        }
        s_Counts.reject_count[ev]++;
        return -2;

    case SCENE_EVENT_STAGE_LOADING:
        /* Advisory event; no state change. Cohort 5+ may insert a
         * synthetic LAYER_LOADING here if a loading screen is wanted.*/
        return 0;

    case SCENE_EVENT_STAGE_READY:
    case SCENE_EVENT_GAMEPLAY_START:
        /* Ensure GAMEPLAY is the top layer. If not, pop down to BOOT
         * and push GAMEPLAY. Idempotent when GAMEPLAY is already top. */
        if (inputLayerTopType() == LAYER_GAMEPLAY) {
            return 0;
        }
        inputLayerAbort(inputLayerDepth() - 1, /* stage transition */ 0);
        if (inputLayerTopType() == LAYER_BOOT) {
            inputLayerPush(&g_LayerGameplay, NULL);
            return 0;
        }
        s_Counts.reject_count[ev]++;
        return -3;

    case SCENE_EVENT_CUTSCENE_START:
        /* Push CUTSCENE on top of whatever is current. Idempotent: a
         * second CUTSCENE_START while already on CUTSCENE returns 0
         * without pushing (single-instance per Cohort 3 model). */
        (void)payload; /* Cohort 3 ignores anim_num / mask */
        if (s_CutsceneHandle != NULL) {
            return 0; /* idempotent */
        }
        s_CutsceneHandle = inputLayerPush(&g_LayerCutscene, NULL);
        if (!s_CutsceneHandle) {
            s_Counts.reject_count[ev]++;
            return -4;
        }
        return 0;

    case SCENE_EVENT_CUTSCENE_END:
        scenePopTrackedLayer(&s_CutsceneHandle, ev);
        return 0;

    case SCENE_EVENT_PAUSE_OPEN:
        if (s_PauseHandle != NULL) {
            return 0; /* already paused */
        }
        s_PauseHandle = inputLayerPush(&g_LayerMenu, NULL);
        if (!s_PauseHandle) {
            s_Counts.reject_count[ev]++;
            return -5;
        }
        return 0;

    case SCENE_EVENT_PAUSE_CLOSE:
        scenePopTrackedLayer(&s_PauseHandle, ev);
        return 0;

    case SCENE_EVENT_VEHICLE_BOARD:
        if (s_VehicleHandle != NULL) {
            return 0;
        }
        {
            const SceneVehiclePayload *p = (const SceneVehiclePayload *)payload;
            const LayerDef *def = (p && p->vehicle_kind == SCENE_VEHICLE_KIND_TURRET)
                                  ? &g_LayerVehicleTurret
                                  : &g_LayerVehicleDriver;
            s_VehicleHandle = inputLayerPush(def, NULL);
            if (!s_VehicleHandle) {
                s_Counts.reject_count[ev]++;
                return -6;
            }
        }
        return 0;

    case SCENE_EVENT_VEHICLE_DISMOUNT:
        scenePopTrackedLayer(&s_VehicleHandle, ev);
        return 0;

    case SCENE_EVENT_OBSERVER_ENTER:
        if (s_ObserverHandle != NULL) {
            return 0;
        }
        {
            const SceneObserverPayload *p = (const SceneObserverPayload *)payload;
            s_ObserverSource = p ? p->source : -1;
            s_ObserverPayload.source = s_ObserverSource;
        }
        s_ObserverHandle = inputLayerPush(&g_LayerObserver, &s_ObserverPayload);
        if (!s_ObserverHandle) {
            s_ObserverSource = -1;
            s_Counts.reject_count[ev]++;
            return -7;
        }
        return 0;

    case SCENE_EVENT_OBSERVER_EXIT:
        if (s_ObserverHandle != NULL && payload != NULL) {
            const SceneObserverPayload *p = (const SceneObserverPayload *)payload;
            if (s_ObserverSource >= 0 && p->source >= 0 && p->source != s_ObserverSource) {
                return 0;
            }
        }
        scenePopTrackedLayer(&s_ObserverHandle, ev);
        s_ObserverSource = -1;
        s_ObserverPayload.source = -1;
        return 0;

    case SCENE_EVENT_STAGE_TEARDOWN:
        /* Unwind everything except BOOT. */
        s_CutsceneHandle = NULL;
        s_PauseHandle    = NULL;
        s_VehicleHandle  = NULL;
        s_ObserverHandle = NULL;
        s_ObserverSource = -1;
        s_ObserverPayload.source = -1;
        if (inputLayerDepth() > 1) {
            inputLayerAbort(inputLayerDepth() - 1, /* teardown */ 0);
        }
        return 0;

    case SCENE_EVENT_DISCONNECT:
        /* Net disconnect: unwind everything to BOOT (main menu push
         * is the next caller's responsibility). */
        s_CutsceneHandle = NULL;
        s_PauseHandle    = NULL;
        s_VehicleHandle  = NULL;
        s_ObserverHandle = NULL;
        s_ObserverSource = -1;
        s_ObserverPayload.source = -1;
        if (inputLayerDepth() > 1) {
            inputLayerAbort(inputLayerDepth() - 1, /* disconnect */ 0);
        }
        return 0;

    default:
        s_Counts.reject_count[ev]++;
        return -8;
    }
}

/* ============================================================
 * Test instrumentation
 * ============================================================ */

void sceneInstrumentReset(SceneFireCounts *out)
{
    if (out) *out = s_Counts;
    memset(&s_Counts, 0, sizeof(s_Counts));
}

const SceneFireCounts *sceneInstrumentGet(void)
{
    return &s_Counts;
}
