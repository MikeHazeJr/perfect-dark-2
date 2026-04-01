/**
 * netmanifest.h -- Match manifest types for the match startup pipeline.
 *
 * The match manifest is a flat list of assets required by the server for
 * an upcoming match.  The server builds it (Phase B), broadcasts it via
 * SVC_MATCH_MANIFEST, and each client checks its local catalog against it
 * (Phase C) before responding with CLC_MANIFEST_STATUS.
 *
 * Wire format (SVC_MATCH_MANIFEST):
 *   u8   opcode = 0x62
 *   u32  manifest_hash     FNV-1a over all entries
 *   u16  num_entries
 *   [for each entry]:
 *     u32  net_hash        FNV-1a hash of the asset
 *     u8   type            MANIFEST_TYPE_*
 *     u8   slot_index      participant slot, or MANIFEST_SLOT_MATCH
 *     str  id              catalog string ID (null-terminated)
 *
 * Phase A: struct + constants defined here.
 * Phase B: manifestBuild() populates from room state + catalog queries.
 * Phase C: client parses, checks local catalog, sends CLC_MANIFEST_STATUS.
 */

#ifndef _IN_NETMANIFEST_H
#define _IN_NETMANIFEST_H

#include <PR/ultratypes.h>

/* Forward declaration for Phase B/E */
struct hub_room_s;
struct matchconfig;

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** slot_index sentinel — asset belongs to the match as a whole, not a participant slot */
#define MANIFEST_SLOT_MATCH  0xFF

/** Initial heap capacity for a newly allocated manifest entries array.
 * The array grows by doubling until it reaches MANIFEST_MAX_ENTRIES. */
#define MANIFEST_INITIAL_CAPACITY 64

/** Hard cap on entries in one manifest.
 *
 * Default 4096; overridable at runtime via pd.ini [Debug] ManifestMaxEntries.
 * Attempts to add beyond the configured cap log an error and are dropped.
 * The array is heap-allocated and grows by doubling from MANIFEST_INITIAL_CAPACITY. */
#define MANIFEST_MAX_ENTRIES 4096

/** Entry type codes (match_manifest_entry_t::type) */
#define MANIFEST_TYPE_BODY       0  /**< Character body model */
#define MANIFEST_TYPE_HEAD       1  /**< Character head model */
#define MANIFEST_TYPE_STAGE      2  /**< Stage geometry + tiles */
#define MANIFEST_TYPE_WEAPON     3  /**< Weapon model / data */
#define MANIFEST_TYPE_COMPONENT  4  /**< Mod component (arbitrary) */
#define MANIFEST_TYPE_MODEL      5  /**< Prop/environment model (ASSET_MODEL catalog entry) */
#define MANIFEST_TYPE_ANIM       6  /**< Animation data (anim_%d catalog entry) */
#define MANIFEST_TYPE_TEXTURE    7  /**< Texture data (tex_%d catalog entry) */
#define MANIFEST_TYPE_LANG       8  /**< Language string bank (base:lang_* catalog entry) */

/** Client response status codes for CLC_MANIFEST_STATUS */
#define MANIFEST_STATUS_READY       0  /**< All listed assets present; ready to load */
#define MANIFEST_STATUS_NEED_ASSETS 1  /**< One or more assets missing; requesting transfer */
#define MANIFEST_STATUS_DECLINE     2  /**< Client declines; will spectate from lobby */

/** Server countdown phase codes for SVC_MATCH_COUNTDOWN */
#define MANIFEST_PHASE_CHECKING     0  /**< Clients checking local catalogs */
#define MANIFEST_PHASE_TRANSFERRING 1  /**< Server distributing missing assets */
#define MANIFEST_PHASE_WAITING      2  /**< All transfers done; waiting for final READY */
#define MANIFEST_PHASE_LOADING      3  /**< All clients READY; match loading */

/* -------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

/**
 * One asset entry in the match manifest.
 *
 * net_hash is the primary identity key used for catalog lookups.
 * id is carried for logging and as a human-readable fallback.
 * slot_index ties body/head entries back to a specific participant slot;
 * match-level assets (stage, weapons, components) use MANIFEST_SLOT_MATCH.
 */
typedef struct {
    u32  net_hash;    /**< FNV-1a hash of the asset — primary catalog key */
    char id[64];      /**< Asset catalog string ID */
    u8   type;        /**< MANIFEST_TYPE_* */
    u8   slot_index;  /**< Participant slot this entry belongs to, or MANIFEST_SLOT_MATCH */
} match_manifest_entry_t;

/**
 * Full match manifest — flat list of all assets required by the server.
 *
 * Built server-side by manifestBuild() (Phase B) and sent to every room
 * member via SVC_MATCH_MANIFEST.  Clients store a copy locally during
 * CLSTATE_PREPARING and use it to drive the catalog check + transfer gate.
 *
 * entries is heap-allocated; starts NULL and is grown on first add.
 * Grows by doubling from MANIFEST_INITIAL_CAPACITY up to the configured cap
 * (default MANIFEST_MAX_ENTRIES, overridable via Debug.ManifestMaxEntries).
 * manifestClear() resets num_entries without freeing the buffer.
 * manifestFree() releases the buffer and zeroes the struct.
 */
typedef struct {
    u32                    manifest_hash;  /**< FNV-1a over all entries; echoed in CLC_MANIFEST_STATUS */
    u16                    num_entries;    /**< Number of valid entries */
    u16                    capacity;       /**< Current allocated size of entries (0 = not yet allocated) */
    match_manifest_entry_t *entries;       /**< Heap-allocated array; NULL until first manifestAddEntry() */
} match_manifest_t;

/* -------------------------------------------------------------------------
 * Phase B: Server-side manifest construction API
 * ------------------------------------------------------------------------- */

/** Server-side manifest, rebuilt on each match start. */
extern match_manifest_t g_ServerManifest;

/** Client-side manifest, populated when SVC_MATCH_MANIFEST arrives. */
extern match_manifest_t g_ClientManifest;

/** Reset a manifest to zero entries.  Retains the allocated buffer for reuse.
 * Safe to call on a zero-initialised struct (entries == NULL). */
void manifestClear(match_manifest_t *m);

/** Free the entries buffer and zero the struct.
 * After this call the manifest is equivalent to zero-initialised. */
void manifestFree(match_manifest_t *m);

/** Return the current configured hard cap (Debug.ManifestMaxEntries, default 4096). */
s32 manifestGetMaxEntries(void);

/** Set the hard cap at runtime and persist to pd.ini.
 * Clamped to [64, MANIFEST_MAX_ENTRIES].  Does not shrink existing allocations. */
void manifestSetMaxEntries(s32 n);

/**
 * Add one entry to the manifest.
 * Deduplicates by net_hash (silently skips duplicates).
 * Internal helper — called by manifestBuild() and test code.
 */
void manifestAddEntry(match_manifest_t *m, u32 net_hash, const char *id,
                      u8 type, u8 slot_index);

/**
 * Build the manifest from current server match state.
 *
 * room: currently unused (Phase B); will scope to room clients in Phase E.
 * cfg:  currently unused (Phase B); will provide client-side config in Phase E.
 *
 * Reads g_MpSetup, g_NetClients[], g_BotConfigsArray[], g_Lobby,
 * assetCatalog, and modmgr.  Calls manifestComputeHash() before returning.
 */
void manifestBuild(match_manifest_t *out, struct hub_room_s *room,
                   const struct matchconfig *cfg);

/**
 * Compute FNV-1a over all manifest entries and store in manifest_hash.
 * Returns the computed hash.
 */
u32 manifestComputeHash(match_manifest_t *m);

/**
 * Dump manifest contents to the system log (Phase B debugging).
 * Logs entry count, manifest hash, and one line per entry.
 */
void manifestLog(const match_manifest_t *m);

/* -------------------------------------------------------------------------
 * Phase C: Client-side manifest check API
 * ------------------------------------------------------------------------- */

/**
 * Check local catalog against the received manifest and respond to server.
 *
 * Called client-side after SVC_MATCH_MANIFEST is received and stored in
 * g_ClientManifest.  For each entry, attempts to resolve the asset via
 * assetCatalogResolveByNetHash() first, then assetCatalogResolve() by ID.
 *
 * Resolution rules:
 *   - MANIFEST_TYPE_COMPONENT: must be present in local catalog.
 *     If missing, added to the NEED_ASSETS list.
 *   - All other types (BODY, HEAD, STAGE, WEAPON): assumed present if not
 *     found (base game assets are always local).  Logs a note but not an error.
 *
 * Sends CLC_MANIFEST_STATUS(READY) if all components resolve, or
 * CLC_MANIFEST_STATUS(NEED_ASSETS, missing_list) otherwise.
 */
void manifestCheck(const match_manifest_t *manifest);

/* -------------------------------------------------------------------------
 * SA-6: SP diff-based asset lifecycle
 * ------------------------------------------------------------------------- */

/**
 * One entry in a manifest diff result.
 * Carries the same id/net_hash/type fields as match_manifest_entry_t but
 * without the MP-specific slot_index (irrelevant for SP diffs).
 */
typedef struct {
    char id[64];      /**< Catalog string ID */
    u32  net_hash;    /**< FNV-1a hash — primary catalog key */
    u8   type;        /**< MANIFEST_TYPE_* */
} manifest_diff_entry_t;

/**
 * Result of comparing two manifests.
 *
 * to_load[]   — entries present in needed but absent from current (load these).
 * to_unload[] — entries present in current but absent from needed (unload these).
 * to_keep[]   — entries present in both (already loaded, no action needed).
 *
 * All three arrays are heap-allocated and grow on demand.
 * manifestDiffFree() frees all three arrays and zeroes the struct.
 * Zero-initialisation is safe (all pointers start NULL).
 */
typedef struct {
    manifest_diff_entry_t *to_load;
    s32                   num_to_load;
    s32                   cap_to_load;
    manifest_diff_entry_t *to_unload;
    s32                   num_to_unload;
    s32                   cap_to_unload;
    manifest_diff_entry_t *to_keep;
    s32                   num_to_keep;
    s32                   cap_to_keep;
} manifest_diff_t;

/** Tracks which assets are currently marked LOADED for the active SP mission. */
extern match_manifest_t g_CurrentLoadedManifest;

/**
 * Build the asset manifest for an SP mission.
 *
 * Queries the catalog for all assets required by stagenum:
 *   - Stage entry (bg, tiles, pads, setup) via catalogResolveStage()
 *   - SP player character body/head (Joanna: body_0 / head_0)
 *
 * TODO SA-6: extend to include character bodies/heads from the stage spawn
 * list once setup file data is available pre-load.
 * TODO SA-6: add prop models used by the stage.
 *
 * Calls manifestComputeHash() before returning.
 */
void manifestBuildMission(s32 stagenum, match_manifest_t *out);

/**
 * Compute a diff between two manifests.
 *
 * Populates *out with:
 *   to_load[]   — in needed, not in current
 *   to_unload[] — in current, not in needed
 *   to_keep[]   — in both
 *
 * Matching is by net_hash only (canonical catalog identity).
 */
void manifestDiff(const match_manifest_t *current,
                  const match_manifest_t *needed,
                  manifest_diff_t *out);

/**
 * Free all three heap arrays in a manifest_diff_t and zero the struct.
 * Safe to call on a zero-initialised struct (pointers start NULL).
 */
void manifestDiffFree(manifest_diff_t *diff);

/**
 * Apply a diff to the load-state tracking layer.
 *
 * For each entry in diff->to_unload: transitions the catalog entry to
 * ASSET_STATE_ENABLED (marks it available but no longer actively loaded).
 *
 * For each entry in diff->to_load: transitions the catalog entry to
 * ASSET_STATE_LOADED (marks it as resident for the upcoming mission).
 *
 * Deep-copies *needed into g_CurrentLoadedManifest so subsequent calls to
 * manifestSPTransition() can diff against the correct baseline.
 *
 * Note: actual memory load/eviction is a future MEM-2 concern.  This call
 * establishes the tracking infrastructure that MEM-2 will hook into.
 */
void manifestApplyDiff(const match_manifest_t *needed,
                       manifest_diff_t *diff);

/**
 * Pre-validation pass: check all to_load entries in a manifest diff.
 *
 * For each entry in diff->to_load:
 *   1. Verifies it exists in the catalog (by ID, then by net_hash fallback).
 *   2. Verifies it is enabled (not toggled off by the user).
 *   3. For MANIFEST_TYPE_LANG: verifies ASSET_LANG type and valid bank_id.
 *   4. Checks declared dependency chain via catalogDepForEach — warns if any
 *      dep is missing or disabled but keeps the parent entry (graceful).
 *
 * Invalid entries are zeroed (id[0] = '\0') so manifestApplyDiff skips them.
 * Always logs a summary line.
 *
 * Returns the count of invalid entries removed from to_load.
 * Returns 0 when all entries are valid or there are none to check.
 *
 * Call between manifestDiff() and manifestApplyDiff() in SP and MP paths.
 */
s32 manifestValidate(manifest_diff_t *diff);

/**
 * Convenience wrapper: build mission manifest, diff against current, apply.
 *
 * Call from mainChangeToStage() for STAGE_IS_GAMEPLAY stages in SP mode
 * (when g_ClientManifest.num_entries == 0).
 * Uses module-internal static buffers — not re-entrant.
 */
void manifestSPTransition(s32 stagenum);

/**
 * Apply a diff-based asset lifecycle transition for an MP match.
 *
 * Uses g_ClientManifest (populated when SVC_MATCH_MANIFEST was received from
 * the server) as the "needed" manifest and diffs it against
 * g_CurrentLoadedManifest.  catalogLoadAsset / catalogUnloadAsset are called
 * for each to_load / to_unload entry respectively.
 *
 * Call from mainChangeToStage() for STAGE_IS_GAMEPLAY stages in MP mode
 * (when g_ClientManifest.num_entries > 0 — i.e. the server has already sent
 * the match manifest via SVC_MATCH_MANIFEST).
 *
 * After returning, g_CurrentLoadedManifest reflects the MP manifest and
 * serves as the baseline for the next stage transition.
 *
 * Uses module-internal static buffers — not re-entrant.
 */
void manifestMPTransition(void);

/**
 * Ensure a single asset is tracked in the active SP manifest.
 *
 * Checks whether catalog_id is already recorded in g_CurrentLoadedManifest.
 * If not, resolves it via assetCatalogResolve(), adds it to the manifest,
 * and transitions its catalog state to ASSET_STATE_LOADED.
 *
 * asset_type: MANIFEST_TYPE_BODY, MANIFEST_TYPE_HEAD, MANIFEST_TYPE_MODEL,
 *             MANIFEST_TYPE_ANIM, or MANIFEST_TYPE_TEXTURE.
 *
 * Returns 1 if the asset is now tracked; 0 if catalog_id is NULL/empty,
 * no SP manifest is active (MP mode or before stage load), or the asset
 * cannot be resolved (synthetic hash is used to suppress repeat warnings).
 *
 * Call from spawn paths (bodyAllocateChr, setupCreateObject) as a safety net
 * for assets that may have been missed by the static pre-scan in
 * manifestBuildMission().  The dedup check is O(n) so it is safe to call on
 * every chr/prop spawn without measurable overhead.
 */
s32 manifestEnsureLoaded(const char *catalog_id, s32 asset_type);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NETMANIFEST_H */
