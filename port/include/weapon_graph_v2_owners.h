#ifndef PD_WEAPON_GRAPH_V2_OWNERS_H
#define PD_WEAPON_GRAPH_V2_OWNERS_H
#include "weapon_graph_v2_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct wg_v2_owners wg_v2_owners;
/* After executor cancel/cleanup, reset remaining host-owned animation pointers
 * before freeing host storage. Called once for each successful binding, even
 * if it never started a shot. Must not throw or re-enter this registry. */
typedef void (*wg_v2_host_destroy)(void *host);
wg_v2_owners *wgV2OwnersCreate(void);
void wgV2OwnersDestroy(wg_v2_owners *);
/* Explicit lifetime producer. Repeating BeginOwner on a still-live address is
 * rejected; retirement must precede reuse. Generations never reset on stage. */
uint64_t wgV2OwnersBeginOwner(wg_v2_owners *, const void *owner);
uint64_t wgV2OwnersGeneration(const wg_v2_owners *, const void *owner);
/* Primary application has exactly the engine's two physical hands. This is
 * unrelated to graph/node storage, which remains dynamic. Host ownership
 * transfers on success only. Bind does not tick or start a native action. */
wg_v2_instance *wgV2OwnersBind(wg_v2_owners *, const void *owner, uint64_t generation,
    int hand, wg_v2_catalog_entry *, const wg_v2_host *, void *host,
    wg_v2_host_destroy, char *error, size_t cap);
wg_v2_instance *wgV2OwnersFind(wg_v2_owners *, const void *owner,
    uint64_t generation, int hand, uint64_t source_generation, void **out_host);
void wgV2OwnersRetireHand(wg_v2_owners *, const void *owner, uint64_t generation,
    int hand, const char *reason);
void wgV2OwnersRetireOwner(wg_v2_owners *, const void *owner, uint64_t generation,
    const char *reason);
/* Pass this directly as wgV2CatalogCreate's retirement hook with owners as host.
 * Only the exact source generation retires; a newer hand cannot be cancelled. */
void wgV2OwnersRetireSource(void *owners, const char *asset_id, uint64_t generation);
void wgV2OwnersRetireAll(wg_v2_owners *, const char *reason);
#ifdef __cplusplus
}
#endif
#endif
