/*
 * scene_pure.c -- pure-C mirror of scene.c for pd-tests.
 * See scene_pure.h for the contract.
 */

#include "scene_pure.h"
#include "inputlayer_pure.h"
#include <stddef.h>
#include <string.h>

static IlpLayerHandle *s_Cutscene = NULL;
static IlpLayerHandle *s_Pause    = NULL;
static IlpLayerHandle *s_Vehicle  = NULL;
static IlpLayerHandle *s_Observer = NULL;

static SpFireCounts s_Counts;

void spInit(void)
{
    s_Cutscene = NULL;
    s_Pause    = NULL;
    s_Vehicle  = NULL;
    s_Observer = NULL;
    memset(&s_Counts, 0, sizeof(s_Counts));
}

void spShutdown(void)
{
    s_Cutscene = NULL;
    s_Pause    = NULL;
    s_Vehicle  = NULL;
    s_Observer = NULL;
}

IlpLayerType spCurrentLayer(void)
{
    return ilpTopType();
}

int spFire(SpSceneEvent ev, const void *payload)
{
    if (ev < 0 || ev >= SP_SCENE_EVENT_COUNT) return -1;

    s_Counts.fire_count[ev]++;

    switch (ev) {
    case SP_SCENE_EVENT_BOOT_COMPLETE:
        ilpAbort(ilpDepth() - 1, 0);
        if (ilpTopType() == ILP_LAYER_BOOT) {
            ilpPush(&g_IlpGameplay, NULL);
            return 0;
        }
        s_Counts.reject_count[ev]++;
        return -2;

    case SP_SCENE_EVENT_STAGE_LOADING:
        return 0;

    case SP_SCENE_EVENT_STAGE_READY:
    case SP_SCENE_EVENT_GAMEPLAY_START:
        if (ilpTopType() == ILP_LAYER_GAMEPLAY) return 0;
        ilpAbort(ilpDepth() - 1, 0);
        if (ilpTopType() == ILP_LAYER_BOOT) {
            ilpPush(&g_IlpGameplay, NULL);
            return 0;
        }
        s_Counts.reject_count[ev]++;
        return -3;

    case SP_SCENE_EVENT_CUTSCENE_START:
        if (s_Cutscene != NULL) return 0;
        s_Cutscene = ilpPush(&g_IlpCutscene, NULL);
        if (!s_Cutscene) { s_Counts.reject_count[ev]++; return -4; }
        return 0;

    case SP_SCENE_EVENT_CUTSCENE_END:
        if (s_Cutscene == NULL) return 0;
        if (ilpPop(s_Cutscene, NULL) != 0) s_Counts.reject_count[ev]++;
        s_Cutscene = NULL;
        return 0;

    case SP_SCENE_EVENT_PAUSE_OPEN:
        if (s_Pause != NULL) return 0;
        s_Pause = ilpPush(&g_IlpMenu, NULL);
        if (!s_Pause) { s_Counts.reject_count[ev]++; return -5; }
        return 0;

    case SP_SCENE_EVENT_PAUSE_CLOSE:
        if (s_Pause == NULL) return 0;
        if (ilpPop(s_Pause, NULL) != 0) s_Counts.reject_count[ev]++;
        s_Pause = NULL;
        return 0;

    case SP_SCENE_EVENT_VEHICLE_BOARD:
        if (s_Vehicle != NULL) return 0;
        {
            const SpSceneVehiclePayload *p = (const SpSceneVehiclePayload *)payload;
            const IlpLayerDef *def = (p && p->vehicle_kind == SP_VEHICLE_KIND_TURRET)
                                     ? &g_IlpVehicleTurret
                                     : &g_IlpVehicleDriver;
            s_Vehicle = ilpPush(def, NULL);
            if (!s_Vehicle) { s_Counts.reject_count[ev]++; return -6; }
        }
        return 0;

    case SP_SCENE_EVENT_VEHICLE_DISMOUNT:
        if (s_Vehicle == NULL) return 0;
        if (ilpPop(s_Vehicle, NULL) != 0) s_Counts.reject_count[ev]++;
        s_Vehicle = NULL;
        return 0;

    case SP_SCENE_EVENT_OBSERVER_ENTER:
        if (s_Observer != NULL) return 0;
        s_Observer = ilpPush(&g_IlpObserver, NULL);
        if (!s_Observer) { s_Counts.reject_count[ev]++; return -7; }
        return 0;

    case SP_SCENE_EVENT_OBSERVER_EXIT:
        if (s_Observer == NULL) return 0;
        if (ilpPop(s_Observer, NULL) != 0) s_Counts.reject_count[ev]++;
        s_Observer = NULL;
        return 0;

    case SP_SCENE_EVENT_STAGE_TEARDOWN:
    case SP_SCENE_EVENT_DISCONNECT:
        s_Cutscene = NULL;
        s_Pause    = NULL;
        s_Vehicle  = NULL;
        s_Observer = NULL;
        if (ilpDepth() > 1) {
            ilpAbort(ilpDepth() - 1, 0);
        }
        return 0;

    default:
        s_Counts.reject_count[ev]++;
        return -8;
    }
}

void spInstrumentReset(SpFireCounts *out)
{
    if (out) *out = s_Counts;
    memset(&s_Counts, 0, sizeof(s_Counts));
}

const SpFireCounts *spInstrumentGet(void)
{
    return &s_Counts;
}
