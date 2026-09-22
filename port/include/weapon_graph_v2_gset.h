#ifndef PD_WEAPON_GRAPH_V2_GSET_H
#define PD_WEAPON_GRAPH_V2_GSET_H
/* Explicit native gset lifetime/copy bridge. struct gset remains four bytes. */
#include "weapon_graph_v2_native.h"
#include "weapon_graph_v2_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
struct gset;
typedef struct wg_v2_gsets wg_v2_gsets;
typedef uint64_t wg_v2_gset_token;
typedef enum wg_v2_gset_status {
    WG_V2_GSET_INVALID = -1, WG_V2_GSET_BASE = 0,
    WG_V2_GSET_SELECTED = 1, WG_V2_GSET_UNSELECTED = 2
} wg_v2_gset_status;
/* Return the v2 catalog ID for this numeric native weapon, including retired
 * source slots until all their gset consumers retire. NULL means non-v2. This
 * must use explicit catalog classification, never infer by current player.
 * Classification slots cannot be rebound to another catalog identity while
 * any gset lease for them lives. The production slot guard remains required. */
typedef const char *(*wg_v2_gset_classify)(void *host, int runtime_weapon);
wg_v2_gsets *wgV2GsetsCreate(wg_v2_gset_classify, void *host);
void wgV2GsetsDestroy(wg_v2_gsets *);
/* Explicit owner lifetime from player/chr registry or an invocation scope.
 * gset must already be initialized. Duplicate live address rejects. Open only
 * registers identity; it does not select an arbitrary action for an idle hand.
 * Returned globally unique token must be closed on every return/retirement.
 * Owner address+generation prevents old teardown from destroying a reused owner. */
wg_v2_gset_token wgV2GsetOpen(wg_v2_gsets *, struct gset *, const void *owner,
    uint64_t owner_generation, char *error, size_t cap);
/* Select only from this live catalog entry's exact prepared native program.
 * Allocations are finished by Open/NativePrepare; Select performs no allocation. */
int wgV2GsetSelect(wg_v2_gsets *, wg_v2_gset_token, wg_v2_catalog_entry *,
    wg_v2_native_bundle *, const wg_v2_native_action *, char *error, size_t cap);
/* Copy all four native bytes AND selected immutable bundle/action into an
 * already-open destination lifetime. Base copies clear old action identity.
 * A missing v2 source registration, mismatched identity or foreign token fails
 * without modifying destination. Copies remain valid after source retirement
 * and subsequent hand selection; they never consult the firing hand again. */
int wgV2GsetCopy(wg_v2_gsets *, wg_v2_gset_token destination,
    const struct gset *source, char *error, size_t cap);
wg_v2_gset_status wgV2GsetLookup(const wg_v2_gsets *, const struct gset *,
    const wg_v2_native_action **out_action);
void wgV2GsetClose(wg_v2_gsets *, wg_v2_gset_token);
void wgV2GsetsRetireOwner(wg_v2_gsets *, const void *owner, uint64_t generation);
void wgV2GsetsRetireAll(wg_v2_gsets *);
size_t wgV2GsetsCount(const wg_v2_gsets *);
#ifdef __cplusplus
}
#endif
#endif
