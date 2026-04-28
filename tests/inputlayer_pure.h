/*
 * inputlayer_pure.h -- pure-C subset of port/src/inputlayer.c for pd-tests.
 *
 * Mirrors the LayerType enum + LayerDef + LayerHandle stack mechanics.
 * No SDL, no actionmap, no IMC -- the same code path as the production
 * inputlayer.c but with all callback signatures kept pure (no dependency
 * on the full InputAction enum). Tests assert push / pop / abort
 * cascade / exclusivity / handle-validity invariants.
 *
 * @SYNC port/src/inputlayer.c
 * @SYNC context/designs/input-universality-and-transitions-2026-04-27.md SC
 */

#ifndef _IN_INPUTLAYER_PURE_H
#define _IN_INPUTLAYER_PURE_H

#ifdef __cplusplus
extern "C" {
#endif

#define ILP_MAX_DEPTH 16

typedef enum IlpLayerType {
    ILP_LAYER_BOOT = 0,
    ILP_LAYER_GAMEPLAY,
    ILP_LAYER_CUTSCENE,
    ILP_LAYER_MENU,
    ILP_LAYER_VEHICLE_DRIVER,
    ILP_LAYER_VEHICLE_TURRET,
    ILP_LAYER_OBSERVER,
    ILP_LAYER_TYPE_COUNT
} IlpLayerType;

typedef int  (*IlpOnPushFn) (void *payload);
typedef void (*IlpOnPopFn)  (void *result_out);
typedef void (*IlpOnAbortFn)(int reason_code);

typedef struct IlpLayerDef {
    IlpLayerType  type;
    const char   *name;
    IlpOnPushFn   on_push;
    IlpOnPopFn    on_pop;
    IlpOnAbortFn  on_abort;
} IlpLayerDef;

typedef struct IlpLayerHandle IlpLayerHandle;

/* Lifecycle */
void ilpInit(void);
void ilpShutdown(void);

/* Stack API */
IlpLayerHandle *ilpPush(const IlpLayerDef *def, void *payload);
int             ilpPop (IlpLayerHandle *h, void *result_out);
void            ilpAbort(int from_top, int reason_code);

/* Inspection */
IlpLayerHandle      *ilpTop(void);
IlpLayerType         ilpTopType(void);
int                  ilpDepth(void);
int                  ilpHas(IlpLayerType t);
const IlpLayerDef   *ilpHandleDef(const IlpLayerHandle *h);
int                  ilpHandleDistanceFromTop(const IlpLayerHandle *h);

/* Test instrumentation: per-callback invocation counts. */
typedef struct IlpInvokeCounts {
    int push_count;
    int pop_count;
    int abort_count;
    int last_abort_reason;
} IlpInvokeCounts;

void ilpInstrumentReset(IlpInvokeCounts *out);
const IlpInvokeCounts *ilpInstrumentGet(IlpLayerType t);

/* Canonical singletons (instrumented by default for tests). */
extern IlpLayerDef g_IlpBoot;
extern IlpLayerDef g_IlpGameplay;
extern IlpLayerDef g_IlpCutscene;
extern IlpLayerDef g_IlpMenu;
extern IlpLayerDef g_IlpVehicleDriver;
extern IlpLayerDef g_IlpVehicleTurret;
extern IlpLayerDef g_IlpObserver;

#ifdef __cplusplus
}
#endif

#endif /* _IN_INPUTLAYER_PURE_H */
