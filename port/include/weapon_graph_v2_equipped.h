#ifndef PD_WEAPON_GRAPH_V2_EQUIPPED_H
#define PD_WEAPON_GRAPH_V2_EQUIPPED_H
#include "weapon_graph_v2_native.h"
#include "weapon_graph_archive.h"
#ifdef __cplusplus
extern "C" {
#endif
struct weapon;
struct modeldef;
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
    /* Exact immutable model owned by lease. NULL remains source-preparation
     * compatibility only; production queued loading requires this pointer. */
    struct modeldef *modeldef;
    /* Private catalog allocation, never public authored identity. Zero leaves
     * allocation to a later adapter; custom values pin the exact weapon ID. */
    int runtime_weapon;

} wg_v2_equipped_model;
/* settings_file is public pd.weapon_settings.v2 JSON. equipped.ammo is either
 * the original slot-zero object or exactly [slot_zero, slot_one], where null
 * explicitly marks an unused slot. Every action/gate ammo reference must have
 * an equipped slot; -1 explicitly requires no ammunition. The public descriptor
 * owns model placement/track type; JSON must not duplicate those fields.
 * Source preparation only: no pool publication or gameplay mutation. Native
 * functions remain NULL unless optional equipped.modes explicitly declares
 * exactly two null/idle {ammo_slot} records. Idle records have type NONE and
 * cannot fire; they supply reload/function-switch metadata, never a branch.
 * Actual action selection must supply the graph's
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
struct modeldef *wgV2EquippedModeldef(const wg_v2_equipped *);
/* True only when this compiled mode has explicit idle metadata. */
int wgV2EquippedHasIdleMode(const wg_v2_equipped *);
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
/* Public .pdweapon snapshot ingress. Descriptor, selected function graph,
 * settings and canonical archive hash are read from the SAME captured ZIP.
 * The caller supplies the model/dependency leases prepared from that source
 * closure. No private manifest or separately authored runtime file is read.
 * Like Prepare, failure leaves the model lease caller-owned; success transfers
 * it to the returned candidate. This does not publish or bind gameplay. */
wg_v2_catalog_entry *wgV2EquippedCatalogPrepareArchive(wg_v2_catalog *,
    const char *catalog_id, const void *archive_bytes, uint32_t archive_size,
    const char *dependency_sha256, const wg_v2_use *,
    const wg_v2_equipped_model *, wg_v2_native_resolver, void *host,
    wg_v2_equipped **out_equipped, char *error, size_t cap);
/* Prepare a complete weapon mode set from one captured archive. out_entries
 * must have two elements; success returns caller-owned primary and optional
 * secondary entries, ready for wgV2CatalogPublishModes. A failed mode releases
 * all candidates and leaves the single model lease caller-owned. On success
 * that lease is shared by both equipment generations and released exactly once
 * after their final consumer. use.function must be primary (0). */
int wgV2EquippedCatalogPrepareArchiveModes(wg_v2_catalog *, const char *catalog_id,
    const void *archive_bytes, uint32_t archive_size, const char *dependency_sha256,
    const wg_v2_use *, const wg_v2_equipped_model *, wg_v2_native_resolver, void *host,
    wg_v2_catalog_entry *out_entries[2], char *error, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
