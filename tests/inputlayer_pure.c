/*
 * inputlayer_pure.c -- pure-C mirror of inputlayer.c for pd-tests.
 * See inputlayer_pure.h for the contract.
 */

#include "inputlayer_pure.h"
#include <stddef.h>
#include <string.h>

struct IlpLayerHandle {
    int                 slot;
    int                 generation;
    const IlpLayerDef  *def;
    void               *payload;
};

static struct IlpLayerHandle s_Stack[ILP_MAX_DEPTH];
static int                   s_Depth          = 0;
static int                   s_NextGeneration = 1;

/* Per-type instrumentation counts (test scaffolding). */
static IlpInvokeCounts s_Counts[ILP_LAYER_TYPE_COUNT];

static int s_LastPushPayloadInt = 0; /* not used by this module */

/* ============================================================
 * Instrumentation callbacks (one per type)
 *
 * Each canonical singleton wires its on_push/on_pop/on_abort to the
 * corresponding s_Counts slot so tests can assert callback ordering
 * and reason-code propagation.
 * ============================================================ */

static int  pushBoot     (void *p) { (void)p; s_Counts[ILP_LAYER_BOOT].push_count++; return 0; }
static void popBoot      (void *o) { (void)o; s_Counts[ILP_LAYER_BOOT].pop_count++; }
static void abortBoot    (int r)   { s_Counts[ILP_LAYER_BOOT].abort_count++; s_Counts[ILP_LAYER_BOOT].last_abort_reason = r; }

static int  pushGameplay (void *p) { (void)p; s_Counts[ILP_LAYER_GAMEPLAY].push_count++; return 0; }
static void popGameplay  (void *o) { (void)o; s_Counts[ILP_LAYER_GAMEPLAY].pop_count++; }
static void abortGameplay(int r)   { s_Counts[ILP_LAYER_GAMEPLAY].abort_count++; s_Counts[ILP_LAYER_GAMEPLAY].last_abort_reason = r; }

static int  pushCutscene (void *p) { (void)p; s_Counts[ILP_LAYER_CUTSCENE].push_count++; return 0; }
static void popCutscene  (void *o) { (void)o; s_Counts[ILP_LAYER_CUTSCENE].pop_count++; }
static void abortCutscene(int r)   { s_Counts[ILP_LAYER_CUTSCENE].abort_count++; s_Counts[ILP_LAYER_CUTSCENE].last_abort_reason = r; }

static int  pushMenu     (void *p) { (void)p; s_Counts[ILP_LAYER_MENU].push_count++; return 0; }
static void popMenu      (void *o) { (void)o; s_Counts[ILP_LAYER_MENU].pop_count++; }
static void abortMenu    (int r)   { s_Counts[ILP_LAYER_MENU].abort_count++; s_Counts[ILP_LAYER_MENU].last_abort_reason = r; }

static int  pushVDrv     (void *p) { (void)p; s_Counts[ILP_LAYER_VEHICLE_DRIVER].push_count++; return 0; }
static void popVDrv      (void *o) { (void)o; s_Counts[ILP_LAYER_VEHICLE_DRIVER].pop_count++; }
static void abortVDrv    (int r)   { s_Counts[ILP_LAYER_VEHICLE_DRIVER].abort_count++; s_Counts[ILP_LAYER_VEHICLE_DRIVER].last_abort_reason = r; }

static int  pushVTur     (void *p) { (void)p; s_Counts[ILP_LAYER_VEHICLE_TURRET].push_count++; return 0; }
static void popVTur      (void *o) { (void)o; s_Counts[ILP_LAYER_VEHICLE_TURRET].pop_count++; }
static void abortVTur    (int r)   { s_Counts[ILP_LAYER_VEHICLE_TURRET].abort_count++; s_Counts[ILP_LAYER_VEHICLE_TURRET].last_abort_reason = r; }

static int  pushObs      (void *p) { (void)p; s_Counts[ILP_LAYER_OBSERVER].push_count++; return 0; }
static void popObs       (void *o) { (void)o; s_Counts[ILP_LAYER_OBSERVER].pop_count++; }
static void abortObs     (int r)   { s_Counts[ILP_LAYER_OBSERVER].abort_count++; s_Counts[ILP_LAYER_OBSERVER].last_abort_reason = r; }

/* ============================================================
 * Singletons
 * ============================================================ */

IlpLayerDef g_IlpBoot           = { ILP_LAYER_BOOT,            "Boot",          pushBoot,     popBoot,     abortBoot     };
IlpLayerDef g_IlpGameplay       = { ILP_LAYER_GAMEPLAY,        "Gameplay",      pushGameplay, popGameplay, abortGameplay };
IlpLayerDef g_IlpCutscene       = { ILP_LAYER_CUTSCENE,        "Cutscene",      pushCutscene, popCutscene, abortCutscene };
IlpLayerDef g_IlpMenu           = { ILP_LAYER_MENU,            "Menu",          pushMenu,     popMenu,     abortMenu     };
IlpLayerDef g_IlpVehicleDriver  = { ILP_LAYER_VEHICLE_DRIVER,  "VehicleDriver", pushVDrv,     popVDrv,     abortVDrv     };
IlpLayerDef g_IlpVehicleTurret  = { ILP_LAYER_VEHICLE_TURRET,  "VehicleTurret", pushVTur,     popVTur,     abortVTur     };
IlpLayerDef g_IlpObserver       = { ILP_LAYER_OBSERVER,        "Observer",      pushObs,      popObs,      abortObs      };

/* ============================================================
 * Internal helpers
 * ============================================================ */

static int handleIsValid(const IlpLayerHandle *h)
{
    if (!h) return 0;
    if (h->slot < 0 || h->slot >= s_Depth) return 0;
    if (h->generation == 0) return 0;
    if (s_Stack[h->slot].generation != h->generation) return 0;
    return 1;
}

void ilpInstrumentReset(IlpInvokeCounts *out)
{
    (void)out;
    memset(s_Counts, 0, sizeof(s_Counts));
    (void)s_LastPushPayloadInt;
}

const IlpInvokeCounts *ilpInstrumentGet(IlpLayerType t)
{
    if (t < 0 || t >= ILP_LAYER_TYPE_COUNT) return NULL;
    return &s_Counts[t];
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

void ilpInit(void)
{
    if (s_Depth > 0) {
        ilpAbort(s_Depth, -1);
    }
    memset(s_Stack, 0, sizeof(s_Stack));
    s_Depth = 0;
    s_NextGeneration = 1;
    ilpPush(&g_IlpBoot, NULL);
}

void ilpShutdown(void)
{
    if (s_Depth > 0) {
        ilpAbort(s_Depth, -2);
    }
    /* @SYNC inputlayer.c inputLayerShutdown: clear the whole stack, symmetric
     * with ilpInit, so no stale def/payload/generation entries linger after
     * shutdown (B-312 stale-pointer hazard). */
    memset(s_Stack, 0, sizeof(s_Stack));
    s_Depth = 0;
    s_NextGeneration = 1;
}

/* ============================================================
 * Stack
 * ============================================================ */

IlpLayerHandle *ilpPush(const IlpLayerDef *def, void *payload)
{
    if (!def) return NULL;
    if (s_Depth >= ILP_MAX_DEPTH) return NULL;

    int slot = s_Depth;
    struct IlpLayerHandle *h = &s_Stack[slot];
    h->slot       = slot;
    h->generation = s_NextGeneration++;
    if (s_NextGeneration <= 0) s_NextGeneration = 1;
    h->def     = def;
    h->payload = payload;
    s_Depth++;

    if (def->on_push) {
        (void)def->on_push(payload);
    }
    return (IlpLayerHandle *)h;
}

int ilpPop(IlpLayerHandle *h, void *result_out)
{
    if (!handleIsValid(h)) return -1;
    if (h->slot != s_Depth - 1) return -2;

    const IlpLayerDef *def = h->def;
    if (def && def->on_pop) {
        def->on_pop(result_out);
    }
    s_Stack[h->slot].generation = 0;
    s_Stack[h->slot].def        = NULL;
    s_Stack[h->slot].payload    = NULL;
    s_Depth--;
    return 0;
}

void ilpAbort(int from_top, int reason_code)
{
    if (from_top <= 0) return;
    if (from_top > s_Depth) from_top = s_Depth;

    for (int i = 0; i < from_top; i++) {
        int slot = s_Depth - 1;
        if (slot < 0) break;
        const IlpLayerDef *def = s_Stack[slot].def;
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

IlpLayerHandle *ilpTop(void)
{
    if (s_Depth <= 0) return NULL;
    return (IlpLayerHandle *)&s_Stack[s_Depth - 1];
}

IlpLayerType ilpTopType(void)
{
    if (s_Depth <= 0) return ILP_LAYER_TYPE_COUNT;
    const IlpLayerDef *def = s_Stack[s_Depth - 1].def;
    if (!def) return ILP_LAYER_TYPE_COUNT;
    return def->type;
}

int ilpDepth(void)
{
    return s_Depth;
}

int ilpHas(IlpLayerType t)
{
    for (int i = 0; i < s_Depth; i++) {
        const IlpLayerDef *def = s_Stack[i].def;
        if (def && def->type == t) return 1;
    }
    return 0;
}

const IlpLayerDef *ilpHandleDef(const IlpLayerHandle *h)
{
    if (!handleIsValid(h)) return NULL;
    return h->def;
}

int ilpHandleDistanceFromTop(const IlpLayerHandle *h)
{
    if (!handleIsValid(h)) return -1;
    return s_Depth - h->slot;
}
