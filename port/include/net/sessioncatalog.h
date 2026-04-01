/**
 * sessioncatalog.h -- SA-1: Per-match session catalog (wire ID translation layer)
 *
 * Maps catalog string IDs to compact u16 wire IDs for the duration of one match.
 * Built server-side from the match manifest, broadcast via SVC_SESSION_CATALOG (0x67),
 * resolved client-side to local catalog entries.
 *
 * Wire format (SVC_SESSION_CATALOG):
 *   u8   opcode = 0x67
 *   u16  num_entries
 *   [for each entry]:
 *     u16  wire_id       compact match-scoped ID
 *     u8   asset_type    MANIFEST_TYPE_* from netmanifest.h
 *     u32  net_hash      CRC32 of catalog_id (client-side verification)
 *     str  catalog_id    null-terminated catalog string ID
 *
 * Lifecycle:
 *   Server: sessionCatalogBuild()     -- after manifestBuild()
 *           sessionCatalogBroadcast() -- after SVC_MATCH_MANIFEST broadcast
 *           sessionCatalogTeardown()  -- on match end / room reset
 *   Client: sessionCatalogReceive()   -- in SVC_SESSION_CATALOG handler
 *           sessionCatalogTeardown()  -- in SVC_STAGE_END handler
 *
 * Client resolution:
 *   sessionCatalogReceive() resolves each received entry to a local catalog
 *   pointer via assetCatalogResolveByNetHash() (CRC32 primary), with string ID
 *   fallback.  Results are stored in s_LocalTranslation[wire_id] for O(1) lookup
 *   during gameplay via sessionCatalogLocalResolve() / sessionCatalogLocalIsResolved().
 */

#ifndef _IN_SESSIONCATALOG_H
#define _IN_SESSIONCATALOG_H

#include <PR/ultratypes.h>
#include "net/netbuf.h"
#include "net/netmanifest.h"
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum entries in the session catalog (matches manifest limit) */
#define SESSION_CATALOG_MAX_ENTRIES MANIFEST_MAX_ENTRIES

/* Max length of a catalog ID string in this subsystem (mirrors manifest entry id size) */
#define SESSION_CATALOG_ID_LEN 64

/**
 * One entry in the session catalog.
 * Populated server-side from the match manifest; received client-side via SVC_SESSION_CATALOG.
 */
typedef struct {
    u16  wire_id;                               /**< compact match-scoped wire ID */
    u8   asset_type;                            /**< MANIFEST_TYPE_* */
    char catalog_id[SESSION_CATALOG_ID_LEN];    /**< catalog string ID */
    u32  net_hash;                              /**< CRC32 of catalog_id -- client-side verification */
} session_catalog_entry_t;

/**
 * Full session catalog for one match.
 * Populated server-side from manifest, received client-side via SVC_SESSION_CATALOG.
 */
typedef struct {
    u16                     num_entries;
    session_catalog_entry_t entries[SESSION_CATALOG_MAX_ENTRIES];
} session_catalog_t;

/**
 * Client-side translation entry: maps one session wire_id to a local catalog pointer.
 * Stored in s_LocalTranslation[], indexed by wire_id (1-based; index 0 unused).
 */
typedef struct {
    u16                  session_id;   /**< wire_id from server */
    const asset_entry_t *local_entry;  /**< resolved pointer into local catalog, or NULL */
    s32                  resolved;     /**< 1 if local_entry found, 0 if missing */
} session_translation_t;

/** Global session catalog (server and client share the same struct, built/received differently). */
extern session_catalog_t g_SessionCatalog;

/* -------------------------------------------------------------------------
 * Server-side API
 * ------------------------------------------------------------------------- */

/**
 * Build the session catalog from a manifest.
 * Assigns sequential wire_ids starting at 1 (0 is reserved as "no entry").
 * Looks up each entry in the asset catalog to populate net_hash (CRC32).
 * Called server-side after manifestBuild().
 */
void sessionCatalogBuild(const match_manifest_t *manifest);

/**
 * Broadcast the session catalog to all room clients (SVC_SESSION_CATALOG).
 * Wire format includes net_hash per entry for client-side verification.
 * Called server-side after SVC_MATCH_MANIFEST broadcast.
 */
void sessionCatalogBroadcast(void);

/**
 * Look up a wire_id by catalog string ID.
 * Returns 0 if not found.
 */
u16 sessionCatalogGetId(const char *asset_id);

/**
 * Look up a wire_id by CRC32 net_hash.
 * O(n) linear scan. Returns 0 if not found.
 */
u16 sessionCatalogGetIdByHash(u32 net_hash);

/* -------------------------------------------------------------------------
 * Client-side API
 * ------------------------------------------------------------------------- */

/**
 * Receive and parse a session catalog from a network buffer.
 * Resolves each entry to a local catalog pointer via CRC32 hash first,
 * string ID fallback.  Populates s_LocalTranslation[] for O(1) gameplay lookups.
 * Called client-side in the SVC_SESSION_CATALOG handler.
 */
void sessionCatalogReceive(struct netbuf *src);

/**
 * Resolve a session wire_id to a local catalog entry pointer.
 * O(1) array lookup.  Returns NULL if session_id is 0, out of range, or unresolved.
 * Called during gameplay to dereference received session IDs.
 */
const asset_entry_t *sessionCatalogLocalResolve(u16 session_id);

/**
 * Check whether a session wire_id resolved to a local catalog entry.
 * Returns 1 if resolved, 0 if missing or out of range.
 */
s32 sessionCatalogLocalIsResolved(u16 session_id);

/* -------------------------------------------------------------------------
 * Shared API
 * ------------------------------------------------------------------------- */

/**
 * Tear down the session catalog (zero all state including translation table).
 * Called on match end (client: SVC_STAGE_END, server: room reset).
 */
void sessionCatalogTeardown(void);

/**
 * Returns 1 if the session catalog is active (has entries), 0 otherwise.
 */
s32 sessionCatalogIsActive(void);

/**
 * Log the current catalog mapping to the system log.
 * Called server-side after sessionCatalogBuild() for debugging.
 */
void sessionCatalogLogMapping(void);

/**
 * Look up a catalog entry by wire_id.
 * Returns NULL if not found.
 */
const session_catalog_entry_t *sessionCatalogLookupEntry(u16 wire_id);

/**
 * Look up a wire_id by catalog string ID.
 * Alias for sessionCatalogGetId() -- retained for call site compatibility.
 * Returns 0 if not found.
 */
u16 sessionCatalogLookupWireId(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SESSIONCATALOG_H */
