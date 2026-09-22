#ifndef PD_WEAPON_GRAPH_V2_NATIVE_H
#define PD_WEAPON_GRAPH_V2_NATIVE_H
/* Public source to immutable native action records; callers own publication. */
#include "weapon_graph_v2.h"
#include "weapon_graph_v2_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
struct weaponfunc;
struct guncmd;
typedef struct wg_v2_native_bundle wg_v2_native_bundle;
typedef struct wg_v2_native_action wg_v2_native_action;
typedef enum wg_v2_dependency_kind { WG_V2_COMMAND_SOURCE, WG_V2_SOUND_SOURCE } wg_v2_dependency_kind;
typedef struct wg_v2_native_dependency {
    const char *catalog_id;
    const char *source_closure_sha256;
    const struct guncmd *commands;
    int sound_id;
    void *lease;
    void (*release)(void *lease);
} wg_v2_native_dependency;
/* Resolver transfers a lease on success. The lease must own the immutable
 * public source command closure AND pin every native clip/audio slot it uses.
 * A borrowed loaderPool pointer, basename lookup or reusable numeric slot does
 * not meet this contract. Exact ID/type and lowercase closure hash are checked.
 * Callbacks must not throw. On failure out must remain zeroed/caller-owned.
 * catalogGraphNativeResolve in catalog_command_generation.h is the production
 * catalog/provider adapter for these owned command and audio generations. */
typedef int (*wg_v2_native_resolver)(void *host, wg_v2_dependency_kind,
    const char *catalog_id, wg_v2_native_dependency *out, char *error, size_t cap);
/* Every action is prepared, including branches not currently selected. Any
 * missing scalar, unsupported flag, bad nested record, or bad dependency rolls
 * back all prepared records and leases. The immutable program is retained.
 * This is native ACTION preparation, not a complete equipped weapon/ammo record. */
wg_v2_native_bundle *wgV2NativePrepare(wg_v2_program *, wg_v2_native_resolver,
    void *host, char *error, size_t cap);
void wgV2NativeRetain(wg_v2_native_bundle *);
void wgV2NativeRelease(wg_v2_native_bundle *);
const wg_v2_native_action *wgV2NativeFind(const wg_v2_native_bundle *, const char *node_id);
const struct weaponfunc *wgV2NativeFunction(const wg_v2_native_action *);
const weapon_graph_held_function_t *wgV2NativeHeld(const wg_v2_native_action *);
const char *wgV2NativeNodeId(const wg_v2_native_action *);
wg_v2_program *wgV2NativeProgram(const wg_v2_native_bundle *);
const char *wgV2NativeClosureHash(const wg_v2_native_bundle *);
int wgV2NativeContains(const wg_v2_native_bundle *, const wg_v2_native_action *);
/* Real program-first catalog candidate with the prepared action bundle as its
 * owned lease. out_native is borrowed until the retained entry is released.
 * This candidate still needs the complete equipped definition before gameplay
 * publication; this function deliberately does not publish anything. */
wg_v2_catalog_entry *wgV2NativeCatalogPrepare(wg_v2_catalog *, const char *catalog_id,
    const char *graph_json, size_t size, const char *dependency_sha256,
    const wg_v2_use *, wg_v2_native_resolver, void *host,
    wg_v2_native_bundle **out_native, char *error, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
