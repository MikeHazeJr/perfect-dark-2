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

/** Maximum entries in one manifest (covers: 1 stage + 32 participants * 2 models + weapons + components) */
#define MANIFEST_MAX_ENTRIES 128

/** Entry type codes (match_manifest_entry_t::type) */
#define MANIFEST_TYPE_BODY       0  /**< Character body model */
#define MANIFEST_TYPE_HEAD       1  /**< Character head model */
#define MANIFEST_TYPE_STAGE      2  /**< Stage geometry + tiles */
#define MANIFEST_TYPE_WEAPON     3  /**< Weapon model / data */
#define MANIFEST_TYPE_COMPONENT  4  /**< Mod component (arbitrary) */

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
 * Full match manifest — hash table of all assets required by the server.
 *
 * Built server-side by manifestBuild() (Phase B) and sent to every room
 * member via SVC_MATCH_MANIFEST.  Clients store a copy locally during
 * CLSTATE_PREPARING and use it to drive the catalog check + transfer gate.
 */
typedef struct {
    u32                    manifest_hash;               /**< FNV-1a over all entries; echoed in CLC_MANIFEST_STATUS */
    u16                    num_entries;                 /**< Number of valid entries in entries[] */
    match_manifest_entry_t entries[MANIFEST_MAX_ENTRIES];
} match_manifest_t;

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------
 * Phase B: Server-side manifest construction API
 * ------------------------------------------------------------------------- */

/** Server-side manifest, rebuilt on each match start. */
extern match_manifest_t g_ServerManifest;

/** Client-side manifest, populated when SVC_MATCH_MANIFEST arrives. */
extern match_manifest_t g_ClientManifest;

/** Zero out a manifest struct. */
void manifestClear(match_manifest_t *m);

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

#endif /* _IN_NETMANIFEST_H */
