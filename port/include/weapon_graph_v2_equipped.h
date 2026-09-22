#ifndef PD_WEAPON_GRAPH_V2_EQUIPPED_H
#define PD_WEAPON_GRAPH_V2_EQUIPPED_H
#include "weapon_graph_v2_native.h"
#include "weapon_graph_archive.h"
#ifdef __cplusplus
extern "C" {
#endif
struct weapon;
typedef struct wg_v2_equipped wg_v2_equipped;
/* Prepared from descriptor.model_file by the catalog/provider transaction.
 * The lease pins the model slot and immutable source closure. Ownership moves
 * only on successful preparation; failure leaves this lease caller-owned. */
typedef struct wg_v2_equipped_model {
    const char *source_reference;
    const char *catalog_id;
    const char *source_closure_sha256;
    int file_id;
    void *lease;
    void (*release)(void *);
} wg_v2_equipped_model;
/* settings_file is public pd.weapon_settings.v2 JSON. The public descriptor
 * owns model placement/track type; JSON must not duplicate those fields.
 * Source preparation only: no pool publication or gameplay mutation. Native
 * functions remain NULL: actual action selection must supply the graph's
 * immutable function rather than silently choosing its first branch.
 * source_sha256 is the canonical complete public archive hash, not private
 * manifest identity. Resolver has the same pinned command contract as actions. */
wg_v2_equipped *wgV2EquippedPrepare(wg_v2_native_bundle *,
    const weapon_graph_archive_descriptor_t *, const char *source_sha256,
    const char *settings_json, size_t size, const wg_v2_equipped_model *,
    wg_v2_native_resolver, void *host, char *error, size_t cap);
void wgV2EquippedRetain(wg_v2_equipped *);
void wgV2EquippedRelease(wg_v2_equipped *);
const struct weapon *wgV2EquippedWeapon(const wg_v2_equipped *);
wg_v2_native_bundle *wgV2EquippedActions(const wg_v2_equipped *);
const char *wgV2EquippedClosureHash(const wg_v2_equipped *);
/* Reserves the actual catalog program generation first, then prepares actions
 * and equipment as its single owned lease. Does not publish. Model ownership
 * transfers only on success. out_equipped is borrowed from the returned entry. */
wg_v2_catalog_entry *wgV2EquippedCatalogPrepare(wg_v2_catalog *, const char *catalog_id,
    const char *graph_json, size_t graph_size, const char *dependency_sha256,
    const wg_v2_use *, const weapon_graph_archive_descriptor_t *,
    const char *source_sha256, const char *settings_json, size_t settings_size,
    const wg_v2_equipped_model *, wg_v2_native_resolver, void *host,
    wg_v2_equipped **out_equipped, char *error, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
