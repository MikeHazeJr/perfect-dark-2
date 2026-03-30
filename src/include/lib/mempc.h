/**
 * mempc.h — PC persistent memory allocator.
 *
 * Provides malloc-backed allocations that survive stage transitions.
 * Unlike MEMPOOL_STAGE (wiped every stage load) or MEMPOOL_PERMANENT
 * (N64-style bump allocator), these allocations are individually tracked
 * and validated with debug guard canaries.
 *
 * Use for resources that should load once and persist for the entire
 * session: fonts, palettes, UI textures, collider definitions, etc.
 */

#ifndef _IN_LIB_MEMPC_H
#define _IN_LIB_MEMPC_H

#include <ultra64.h>
#include "types.h"

/**
 * Allocate persistent PC memory that survives stage transitions.
 *
 * @param size  Number of bytes to allocate.
 * @param tag   Human-readable label for debugging (e.g. "FontHandelGothicSm").
 * @return      Pointer to zeroed memory, or NULL on failure.
 */
void *mempPCAlloc(u32 size, const char *tag);

/**
 * Validate all persistent allocations.
 * Checks guard canaries for buffer overruns/underruns.
 * Call periodically or at critical transitions (stage load, endscreen).
 *
 * @param context  Label for log messages (e.g. "stageReset", "endscreen").
 * @return         true if all allocations are intact.
 */
bool mempPCValidate(const char *context);

/**
 * Free all persistent allocations (call at shutdown).
 */
void mempPCFreeAll(void);

/**
 * Get total bytes allocated in the persistent pool.
 */
u32 mempPCGetTotalAllocated(void);

/**
 * Get number of persistent allocations.
 */
u32 mempPCGetNumAllocations(void);

#endif
