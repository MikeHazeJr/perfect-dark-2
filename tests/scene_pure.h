/*
 * scene_pure.h -- pure-C subset of port/src/scene.c for pd-tests.
 *
 * Layered on top of inputlayer_pure (the test mirror of
 * port/src/inputlayer.c) so the dispatcher invariants can be
 * asserted without SDL or actionmap dependencies. The contract
 * mirrors port/include/scene.h one-to-one with `sp` prefix.
 *
 * @SYNC port/src/scene.c
 * @SYNC context/designs/input-universality-and-transitions-2026-04-27.md SD
 */

#ifndef _IN_SCENE_PURE_H
#define _IN_SCENE_PURE_H

#include "inputlayer_pure.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SpSceneEvent {
    SP_SCENE_EVENT_BOOT_COMPLETE = 0,
    SP_SCENE_EVENT_STAGE_LOADING,
    SP_SCENE_EVENT_STAGE_READY,
    SP_SCENE_EVENT_GAMEPLAY_START,
    SP_SCENE_EVENT_CUTSCENE_START,
    SP_SCENE_EVENT_CUTSCENE_END,
    SP_SCENE_EVENT_PAUSE_OPEN,
    SP_SCENE_EVENT_PAUSE_CLOSE,
    SP_SCENE_EVENT_VEHICLE_BOARD,
    SP_SCENE_EVENT_VEHICLE_DISMOUNT,
    SP_SCENE_EVENT_OBSERVER_ENTER,
    SP_SCENE_EVENT_OBSERVER_EXIT,
    SP_SCENE_EVENT_STAGE_TEARDOWN,
    SP_SCENE_EVENT_DISCONNECT,
    SP_SCENE_EVENT_COUNT
} SpSceneEvent;

#define SP_VEHICLE_KIND_DRIVER 0
#define SP_VEHICLE_KIND_TURRET 1

typedef struct SpSceneVehiclePayload {
    int vehicle_kind;
} SpSceneVehiclePayload;

void spInit(void);
void spShutdown(void);

int spFire(SpSceneEvent ev, const void *payload);

IlpLayerType spCurrentLayer(void);

typedef struct SpFireCounts {
    int fire_count[SP_SCENE_EVENT_COUNT];
    int reject_count[SP_SCENE_EVENT_COUNT];
} SpFireCounts;

void              spInstrumentReset(SpFireCounts *out);
const SpFireCounts *spInstrumentGet(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SCENE_PURE_H */
