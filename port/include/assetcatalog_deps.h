/**
 * assetcatalog_deps.h -- Phase 2: Catalog dependency graph
 *
 * A lightweight flat table that maps catalog entries to their constituent
 * dependencies.  When a BODY or HEAD entry is added to a manifest all
 * registered deps are automatically expanded so the full character
 * composite (custom anims, custom textures) is loaded together.
 *
 * Population:
 *   - Scanner calls catalogDepRegister() for each "deps" value found in a
 *     body/head component INI file.
 *   - Scanner also calls catalogDepRegister() in reverse when an
 *     ASSET_ANIMATION entry declares a non-empty "target_body" field, so
 *     mods only need to annotate the animation side if preferred.
 *   - Base-game (bundled) entries are never given deps; their assets are
 *     always ROM-resident and the load/unload calls are no-ops.
 *
 * Manifest integration:
 *   - manifestBuildMission() and manifestBuild() call
 *     catalogDepForEach() after each BODY/HEAD addition and add the
 *     resolved dep entries as MANIFEST_TYPE_ANIM / MANIFEST_TYPE_TEXTURE.
 *   - manifestApplyDiff() is unchanged -- deps appear as ordinary entries.
 *   - manifestAddEntry() already deduplicates by net_hash, so a dep shared
 *     between two characters is added only once but both owners reference it.
 *
 * Lifecycle:
 *   - assetCatalogClear()      calls catalogDepClear().
 *   - assetCatalogClearMods()  calls catalogDepClearMods().
 */

#ifndef _IN_ASSETCATALOG_DEPS_H
#define _IN_ASSETCATALOG_DEPS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/**
 * Hard cap on total registered dependency pairs.
 * 256 pairs is well above any realistic mod loadout (a character with
 * 10 custom animations + 5 textures consumes 15 slots).
 */
#define CATALOG_MAX_DEP_PAIRS 256

/* -------------------------------------------------------------------------
 * Iteration callback type
 * ------------------------------------------------------------------------- */

/**
 * Callback invoked by catalogDepForEach() for each dep registered under
 * a given owner.  dep_id is a catalog string ID (e.g. "mod:warrior_idle").
 * userdata is the opaque pointer passed to catalogDepForEach().
 */
typedef void (*CatalogDepIterFn)(const char *dep_id, void *userdata);

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
 *   owners never have meaningful deps since their assets are always
 *   ROM-resident; pass 1 to allow consistent code paths but note that
 *   catalogDepForEach() skips bundled pairs during manifest expansion.
 *
 * Duplicate (owner_id, dep_id) pairs are silently ignored.
 * If the table is full a warning is logged and the pair is dropped.
 */
void catalogDepRegister(const char *owner_id, const char *dep_id,
                        s32 is_bundled);

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
