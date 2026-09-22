#ifndef PD_WEAPON_GRAPH_V2_CATALOG_H
#define PD_WEAPON_GRAPH_V2_CATALOG_H
/* Immutable program ownership unit. Native source preparation and
 * production catalog/hand attachment are separate mandatory application units. */
#include "weapon_graph_v2.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct wg_v2_catalog wg_v2_catalog;
typedef struct wg_v2_catalog_entry wg_v2_catalog_entry;

typedef struct wg_v2_use {
    int network_active;
    int recording_or_replaying;
    int bot_owner;
    int function; /* primary (0) or secondary (1) held function */
} wg_v2_use;

/* Exact generation hook, synchronous, non-reentrant, no allocation/failure.
 * Called before releasing registry ownership so the host can cancel/destroy
 * matching native instances while all their program/dependency storage lives.
 * A NULL hook is rejected; a read-only/editor owner can supply an explicit no-op. */
typedef void (*wg_v2_retire_hook)(void *host, const char *asset_id,
                                uint64_t generation);
typedef void (*wg_v2_lease_release)(void *lease);
/* Candidate-local, no publication or gameplay mutation. Returns a newly owned
 * lease only after all source preparation succeeds; failure cleans its partial
 * work and returns NULL. The input program is retained by the candidate. */
typedef void *(*wg_v2_lease_prepare)(void *host, wg_v2_program *, char *error, size_t cap);

wg_v2_catalog *wgV2CatalogCreate(wg_v2_retire_hook, void *host);
void wgV2CatalogDestroy(wg_v2_catalog *);
/* Admission is reusable at catalog activation AND request/mode-transition
 * boundaries. A later mode transition must reject if any active v2 selection
 * exists; merely rejecting newly compiled candidates is insufficient. */
int wgV2CheckUse(const wg_v2_use *, char *error, size_t cap);
int wgV2CatalogCanEnterUse(const wg_v2_catalog *, const wg_v2_use *,
                         char *error, size_t cap);

/* Caller has already prepared a source-hashed dependency/native-adapter lease.
 * Transfer lease ownership ONLY on success. Compile and identity errors leave
 * it caller-owned. The lease must not refer to mutable/reusable numeric slots.
 * This unit does not claim to implement that native preparation contract. */
wg_v2_catalog_entry *wgV2CatalogPrepare(wg_v2_catalog *, const char *catalog_id,
    const char *graph_json, size_t size, const char *dependency_sha256,
    const wg_v2_use *, void *prepared_lease, wg_v2_lease_release,
    char *error, size_t cap);
/* Program-first preparation avoids a placeholder native lease or separately
 * compiled program with mismatched generation. No failed candidate is active. */
wg_v2_catalog_entry *wgV2CatalogPrepareWithLease(wg_v2_catalog *, const char *catalog_id,
    const char *graph_json, size_t size, const char *dependency_sha256,
    const wg_v2_use *, wg_v2_lease_prepare, void *host, wg_v2_lease_release,
    char *error, size_t cap);
/* Pure final preflight. Prepare has already reserved the map slot. After this
 * passes, the caller can publish its fully prepared native adapter and program
 * in one synchronous main-thread transaction with no intervening callback that
 * changes candidates. Publish makes no allocations and cannot fail if that
 * preflight remains unchanged. It keeps the caller's retained entry handle. */
int wgV2CatalogCanPublish(const wg_v2_catalog *, const wg_v2_catalog_entry *,
    char *error, size_t cap);
int wgV2CatalogPublish(wg_v2_catalog *, wg_v2_catalog_entry *, char *error, size_t cap);
wg_v2_catalog_entry *wgV2CatalogAcquire(wg_v2_catalog *, const char *catalog_id);
/* Exact mode selection. The legacy Acquire entry point selects primary. */
wg_v2_catalog_entry *wgV2CatalogAcquireFunction(wg_v2_catalog *,
    const char *catalog_id, int function);
int wgV2CatalogRetire(wg_v2_catalog *, const char *catalog_id);
int wgV2CatalogRetireFunction(wg_v2_catalog *, const char *catalog_id,
    int function);
void wgV2CatalogRetireAll(wg_v2_catalog *);
size_t wgV2CatalogCount(const wg_v2_catalog *);

void wgV2CatalogEntryRetain(wg_v2_catalog_entry *);
void wgV2CatalogEntryRelease(wg_v2_catalog_entry *);
const char *wgV2CatalogEntryAssetId(const wg_v2_catalog_entry *);
int wgV2CatalogEntryFunction(const wg_v2_catalog_entry *);
uint64_t wgV2CatalogEntryGeneration(const wg_v2_catalog_entry *);
int wgV2CatalogEntryAccepting(const wg_v2_catalog_entry *);
/* Borrowed program/lease remain alive until the retained entry is released. */
wg_v2_program *wgV2CatalogEntryProgram(const wg_v2_catalog_entry *);
void *wgV2CatalogEntryLease(const wg_v2_catalog_entry *);

#ifdef __cplusplus
}
#endif
#endif
