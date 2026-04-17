/**
 * forge_runtime.h -- The Grid runtime prop registry (F1+ instantiation layer).
 *
 * Bridges the forge editor data model (forge_object_t pool in forge_core.c)
 * and the live engine when switching FREEFLY -> NORMAL play mode.
 *
 * On forgeRuntimeEnterPlay():
 *   - SPAWN_POINT objects  -> injected into the global spawn pool via
 *                             spawnPoolAppendForgePoints().
 *   - AI objects           -> allocated via botmgrAllocateBot().
 *   - Bot-tab requests     -> consumed by forgeRuntimeTick() each frame.
 *   - Props / weapons / zones -> logged; model / trigger wire deferred.
 *
 * On forgeRuntimeExitPlay():
 *   - Forge-spawned bots removed via botmgrRemoveAll().
 *   - Spawn pool rebuilt without forge-injected points.
 *   - Any directly-allocated props freed via propFree().
 *
 * The logic system (forge_logic.c) uses forgeRuntimeFindPropByUid() and
 * forgeRuntimeSpawnBotAt() so TELEPORT_PLAYER / SPAWN_AI / ENABLE_OBJECT
 * actions reach the corresponding live engine state.
 */

#ifndef _IN_FORGE_RUNTIME_H
#define _IN_FORGE_RUNTIME_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum simultaneously tracked forge-instantiated engine objects. */
#define FORGE_RUNTIME_MAX_HANDLES  256

/* One record per forge_object that was materialised into the engine. */
typedef struct forge_prop_handle {
    u32          forge_uid;      /* forge_object.uid this was spawned from */
    struct prop *prop;           /* live engine prop (NULL for spawn/bot-only) */
    u8           is_spawn_point; /* 1 = injected into spawn pool, not a prop */
    u8           is_bot;         /* 1 = spawned via botmgrAllocateBot */
    u8           pad[2];
} forge_prop_handle_t;

/* ------------------------------------------------------------------
 * Lifecycle -- called from forgemode.c at mode transition points.
 * ------------------------------------------------------------------ */

/* FREEFLY -> NORMAL: instantiate all enabled placed objects. */
void forgeRuntimeEnterPlay(void);

/* NORMAL -> FREEFLY: clean up all forge-instantiated engine state. */
void forgeRuntimeExitPlay(void);

/* Per-tick: consume pending bot-add / bot-remove from the Bots tab. */
void forgeRuntimeTick(void);

/* ------------------------------------------------------------------
 * Logic system helpers -- used by forge_logic.c action dispatch.
 * ------------------------------------------------------------------ */

/* Return live engine prop for a forge uid, or NULL if not spawned as a prop. */
struct prop *forgeRuntimeFindPropByUid(u32 uid);

/* Spawn a bot at the position of the forge AI object with uid. */
void forgeRuntimeSpawnBotAt(u32 forge_uid);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_FORGE_RUNTIME_H */
