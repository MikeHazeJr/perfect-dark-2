/**
 * assetcatalog_deps.h -- Phase 2: Catalog dependency graph
 *
 * A lightweight dynamically-grown table that maps catalog entries to their
 * constituent dependencies. Manifest builders call catalogDepForEach() at
 * known owner boundaries so registered deps can be added with the owning
 * composite or behavior chain.
 *
 * Population:
 *   - Scanner and distribution ingestion call catalogDepRegister() for each
 *     "deps" value found in a body/head/projectile/entity component INI file.
 *   - Scanner and distribution ingestion call catalogDepRegister() when a projectile declares
 *     entity_ref / transition_entity.
 *   - Scanner and distribution ingestion also call catalogDepRegister() in reverse when an
 *     ASSET_ANIMATION entry declares a non-empty "target_body" field, so
 *     mods only need to annotate the animation side if preferred.
 *   - Bundled dependency pairs may record public source closure, but manifest
 *     expansion skips them because bundled catalog rows are process-lifetime.
 *
 * Manifest integration:
 *   - manifestBuildMission() and manifestBuild() call
 *     catalogDepForEach() after manifest additions and add resolved dep
 *     entries using the dep asset's manifest type.
 *   - manifestApplyDiff() is unchanged -- deps appear as ordinary entries.
 *   - manifestAddEntry() already deduplicates by net_hash (internal cache key),
 *     so a dep shared between two characters is added only once but both owners reference it.
 *
 * Lifecycle:
 *   - assetCatalogClear()      calls catalogDepClear().
 *   - assetCatalogClearMods()  calls catalogDepClearMods().
 */

#ifndef _IN_ASSETCATALOG_DEPS_H
#define _IN_ASSETCATALOG_DEPS_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/**
 * Initial allocation for registered dependency pairs. This is not a content
 * cap; the table grows so large extracted installs and custom packs do not
 * drop public source dependency closure after the first allocation.
 */
#define CATALOG_INITIAL_DEP_PAIRS 256

/* -------------------------------------------------------------------------
 * Iteration callback type
 * ------------------------------------------------------------------------- */

/**
 * Callback invoked by catalogDepForEach() for each dep registered under
 * a given owner.  dep_id is a catalog string ID (e.g. "mod:warrior_idle").
 * userdata is the opaque pointer passed to catalogDepForEach().
 */
typedef void (*CatalogDepIterFn)(const char *dep_id, void *userdata);
typedef void (*CatalogDepTypedIterFn)(const char *dep_id,
	asset_type_e expected_type, void *userdata);
typedef s32 (*CatalogDepKeepFn)(const char *dep_id,
	asset_type_e expected_type, void *userdata);

/* Opaque deep snapshot used by scanner-owned admission transactions. The
 * snapshot captures every edge because a descriptor may register a reverse
 * edge whose owner lives outside the received package (for example an
 * animation targeting an existing body). */
typedef struct catalog_dep_snapshot {
	void *pairs;
	s32 count;
} catalog_dep_snapshot_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * Register a dependency pair.
 *
 * When owner_id is manifested, dep_id will be resolved and added to the
 * same manifest.  Both arguments are catalog string IDs.
 *
 * is_bundled: 1 if the owner is a base-game (bundled) asset.  Bundled
 *   owners are process-lifetime; pass 1 to record source closure while
 *   catalogDepForEach() skips those pairs during manifest expansion.
 *
 * Duplicate (owner_id, dep_id) pairs are silently ignored. Allocation failure
 * logs a warning and drops only that pair.
 */
void catalogDepRegister(const char *owner_id, const char *dep_id,
                        s32 is_bundled);
s32 catalogDepRegisterTyped(const char *owner_id, const char *dep_id,
	asset_type_e expected_type, s32 is_bundled);

/* Remove one exact dependency edge. Used only by transactional archive
 * registration rollback; returns 1 if an edge was removed. */
s32 catalogDepUnregister(const char *owner_id, const char *dep_id);
/* Remove stale edges for one successfully replaced public owner. The keep
 * callback describes the exact newly admitted typed source set. */
void catalogDepPruneOwner(const char *owner_id, CatalogDepKeepFn keep,
	void *userdata);
s32 catalogDepContains(const char *owner_id, const char *dep_id);
asset_type_e catalogDepExpectedType(const char *owner_id, const char *dep_id);

/** Ensure capacity for additional dependency pairs without mutating graph
 * truth. Transactional composite registrars reserve before child rows become
 * visible so later edge commits cannot be dropped by allocation failure. */
s32 catalogDepReserve(s32 additional);

/* Capture/restore the exact edge table around an all-or-nothing catalog
 * admission. Restore consumes no source paths and does not activate assets. */
s32 catalogDepSnapshotCreate(catalog_dep_snapshot_t *snapshot);
s32 catalogDepSnapshotRestore(const catalog_dep_snapshot_t *snapshot);
void catalogDepSnapshotDestroy(catalog_dep_snapshot_t *snapshot);

/**
 * Iterate all deps registered for owner_id.
 *
 * Calls fn(dep_id, userdata) for each dep pair whose owner matches
 * owner_id (by FNV-1a hash then string comparison).
 * Bundled pairs are skipped -- they exist only as metadata and are never
 * loaded/unloaded.
 * Safe to call when no deps are registered (loop simply does not fire).
 */
void catalogDepForEach(const char *owner_id,
                       CatalogDepIterFn fn, void *userdata);
/* Runtime lifecycle iterator. Unlike the manifest iterator above, this emits
 * bundled edges because public-source activation must validate them too. */
void catalogDepForEachTyped(const char *owner_id,
	CatalogDepTypedIterFn fn, void *userdata);

/**
 * Remove all dep pairs where is_bundled == 0 (mod assets).
 * Call from assetCatalogClearMods() when mod entries are evicted.
 */
void catalogDepClearMods(void);

/**
 * Remove all dep pairs from the table.
 * Call from assetCatalogClear() on a full catalog reset.
 */
void catalogDepClear(void);

/**
 * Return the number of dep pairs currently registered.
 * Used by the settings UI catalog stats panel.
 */
s32 catalogDepCount(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ASSETCATALOG_DEPS_H */
